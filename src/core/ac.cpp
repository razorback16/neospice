#include "core/ac.hpp"
#include "core/newton.hpp"
#include "core/convergence.hpp"
#include "core/klu_solver.hpp"
#include "devices/vsource.hpp"
#include "devices/inductor.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace neospice {

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

// Generate frequency points based on mode
static std::vector<double> generate_frequencies(AnalysisCommand::ACMode mode,
                                                 int npoints, double fstart, double fstop) {
    std::vector<double> freqs;
    if (fstart <= 0 || fstop <= 0 || fstop < fstart || npoints < 1)
        return freqs;

    switch (mode) {
    case AnalysisCommand::DEC: {
        double decades = std::log10(fstop / fstart);
        int total = static_cast<int>(std::round(decades * npoints)) + 1;
        freqs.reserve(total);
        for (int i = 0; i < total; ++i) {
            double f = fstart * std::pow(10.0, static_cast<double>(i) / npoints);
            freqs.push_back(f);
        }
        break;
    }
    case AnalysisCommand::OCT: {
        double octaves = std::log2(fstop / fstart);
        int total = static_cast<int>(std::round(octaves * npoints)) + 1;
        freqs.reserve(total);
        for (int i = 0; i < total; ++i) {
            double f = fstart * std::pow(2.0, static_cast<double>(i) / npoints);
            freqs.push_back(f);
        }
        break;
    }
    case AnalysisCommand::LIN: {
        freqs.reserve(npoints);
        double step = (npoints > 1) ? (fstop - fstart) / (npoints - 1) : 0.0;
        for (int i = 0; i < npoints; ++i) {
            freqs.push_back(fstart + i * step);
        }
        break;
    }
    }
    return freqs;
}

ACResult solve_ac(Circuit& ckt, AnalysisCommand::ACMode mode,
                  int npoints, double fstart, double fstop) {
    const int32_t n = ckt.num_vars();
    const int32_t num_nodes = ckt.num_nodes();

    // 1. DC operating point
    // Initial guess: zeros + .nodeset hints; .ic as fallback for unpinned nodes.
    // .nodeset wins when both are set.  .ic doubles as a Newton seed hint here
    // so circuits that ship .ic start DC from a feasible point instead of
    // all-zero (where subthreshold gm/gds vanish).
    std::vector<double> dc_solution(n, 0.0);
    std::vector<char> pinned(n, 0);
    for (auto& [node_idx, value] : ckt.nodeset) {
        if (node_idx >= 0 && node_idx < n) {
            dc_solution[node_idx] = value;
            pinned[node_idx] = 1;
        }
    }
    for (auto& [node_idx, value] : ckt.ic) {
        if (node_idx >= 0 && node_idx < n && !pinned[node_idx]) {
            dc_solution[node_idx] = value;
        }
    }

    KLUSolver dc_solver;
    dc_solver.symbolic(ckt.pattern());

    auto result = newton_solve(ckt, dc_solver, dc_solution, ckt.options);
    if (result.converged) {
        dc_solution = result.solution;
    } else {
        result = gmin_stepping(ckt, dc_solver, dc_solution, ckt.options);
        if (result.converged) {
            dc_solution = result.solution;
        } else {
            result = source_stepping(ckt, dc_solver, dc_solution, ckt.options);
            if (result.converged) {
                dc_solution = result.solution;
            } else {
                throw ConvergenceError("AC analysis: DC operating point failed to converge");
            }
        }
    }

    // 2. Build G and C matrices using the DC sparsity pattern
    const auto& pattern = ckt.pattern();
    NumericMatrix G(pattern);
    NumericMatrix C(pattern);
    G.clear();
    C.clear();

    for (auto& dev : ckt.devices()) {
        dev->ac_stamp(dc_solution, G, C);
    }

    // 3. Build AC excitation vector (complex RHS)
    std::vector<std::complex<double>> ac_rhs(n, {0.0, 0.0});
    for (auto& dev : ckt.devices()) {
        if (auto* vs = dynamic_cast<VSource*>(dev.get())) {
            if (vs->ac_mag() != 0.0) {
                int32_t br = vs->branch_index();
                if (br >= 0 && br < n) {
                    ac_rhs[br] = std::polar(vs->ac_mag(), vs->ac_phase_rad());
                }
            }
        }
    }

    // 4. Generate frequency points
    auto freqs = generate_frequencies(mode, npoints, fstart, fstop);
    if (freqs.empty()) {
        return ACResult{};
    }

    // 5. Build 2n x 2n sparsity pattern ONCE
    //    Layout: rows/cols 0..n-1 are real part, n..2n-1 are imaginary part
    //    [Re(A)  -Im(A)] [Re(x)]   [Re(b)]
    //    [Im(A)   Re(A)] [Im(x)] = [Im(b)]
    const int32_t n2 = 2 * n;
    SparsityBuilder builder(n2);

    const auto& entries = pattern.entries();
    for (const auto& [r, c] : entries) {
        // Re(A) block: top-left (r, c) and bottom-right (r+n, c+n)
        builder.add(r, c);
        builder.add(r + n, c + n);
        // -Im(A) block: top-right (r, c+n) and Im(A) block: bottom-left (r+n, c)
        builder.add(r, c + n);
        builder.add(r + n, c);
    }

    auto pattern_2n = builder.build();
    NumericMatrix mat_2n(pattern_2n);

    // Symbolic factorization ONCE
    KLUSolver ac_solver;
    ac_solver.symbolic(pattern_2n);

    // Prepare result
    ACResult ac_result;
    ac_result.frequency = freqs;

    // Initialize voltage/current result vectors
    for (int32_t i = 0; i < num_nodes; ++i) {
        std::string key = "v(" + to_lower(ckt.node_name(i)) + ")";
        ac_result.voltages[key].resize(freqs.size());
    }
    for (auto& dev : ckt.devices()) {
        if (auto* vs = dynamic_cast<VSource*>(dev.get())) {
            std::string key = "i(" + to_lower(dev->name()) + ")";
            ac_result.currents[key].resize(freqs.size());
        } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
            std::string key = "i(" + to_lower(dev->name()) + ")";
            ac_result.currents[key].resize(freqs.size());
        }
    }

    // 6. Frequency sweep
    for (size_t fi = 0; fi < freqs.size(); ++fi) {
        double omega = 2.0 * M_PI * freqs[fi];

        // Build 2n x 2n numeric matrix
        mat_2n.clear();

        for (const auto& [r, c] : entries) {
            double g_val = G.value(pattern.offset(r, c));
            double c_val = C.value(pattern.offset(r, c));

            double re_a = g_val;           // Re(G + jwC) = G
            double im_a = omega * c_val;   // Im(G + jwC) = wC

            // Top-left: Re(A)
            mat_2n.add(pattern_2n.offset(r, c), re_a);
            // Bottom-right: Re(A)
            mat_2n.add(pattern_2n.offset(r + n, c + n), re_a);
            // Top-right: -Im(A)
            mat_2n.add(pattern_2n.offset(r, c + n), -im_a);
            // Bottom-left: Im(A)
            mat_2n.add(pattern_2n.offset(r + n, c), im_a);
        }

        // Build 2n RHS
        std::vector<double> rhs_2n(n2, 0.0);
        for (int32_t i = 0; i < n; ++i) {
            rhs_2n[i]     = ac_rhs[i].real();
            rhs_2n[i + n] = ac_rhs[i].imag();
        }

        // Factorize and solve
        if (fi == 0) {
            ac_solver.numeric(pattern_2n, mat_2n);
        } else {
            ac_solver.refactorize(mat_2n);
        }
        ac_solver.solve(rhs_2n);

        // Extract complex solution
        for (int32_t i = 0; i < num_nodes; ++i) {
            std::string key = "v(" + to_lower(ckt.node_name(i)) + ")";
            ac_result.voltages[key][fi] = {rhs_2n[i], rhs_2n[i + n]};
        }
        for (auto& dev : ckt.devices()) {
            if (auto* vs = dynamic_cast<VSource*>(dev.get())) {
                int32_t br = vs->branch_index();
                if (br >= 0 && br < n) {
                    std::string key = "i(" + to_lower(dev->name()) + ")";
                    ac_result.currents[key][fi] = {rhs_2n[br], rhs_2n[br + n]};
                }
            } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
                int32_t br = ind->branch_index();
                if (br >= 0 && br < n) {
                    std::string key = "i(" + to_lower(dev->name()) + ")";
                    ac_result.currents[key][fi] = {rhs_2n[br], rhs_2n[br + n]};
                }
            }
        }
    }

    return ac_result;
}

} // namespace neospice
