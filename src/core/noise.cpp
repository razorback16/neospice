#include "core/noise.hpp"
#include "core/newton.hpp"
#include "core/convergence.hpp"
#include "core/klu_solver.hpp"
#include "devices/vsource.hpp"
#include "devices/inductor.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>

namespace neospice {

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

// Generate frequency points based on mode (shared logic with AC)
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

NoiseResult solve_noise(Circuit& ckt,
                        const std::string& output_node,
                        const std::string& input_src,
                        AnalysisCommand::ACMode mode,
                        int npoints, double fstart, double fstop) {
    const int32_t n = ckt.num_vars();

    // ---------------------------------------------------------------
    // 1. Find the output node index
    // ---------------------------------------------------------------
    int32_t out_idx = ckt.node_index(to_lower(output_node));
    if (out_idx < 0) {
        throw std::runtime_error("Noise analysis: output node '" + output_node + "' not found");
    }

    // ---------------------------------------------------------------
    // 2. Find the input voltage source and its branch index
    // ---------------------------------------------------------------
    const VSource* input_vs = nullptr;
    for (const auto& dev : ckt.devices()) {
        if (auto* vs = dynamic_cast<const VSource*>(dev.get())) {
            if (to_lower(vs->name()) == to_lower(input_src)) {
                input_vs = vs;
                break;
            }
        }
    }
    if (!input_vs) {
        throw std::runtime_error("Noise analysis: input source '" + input_src + "' not found");
    }
    int32_t input_branch = input_vs->branch_index();
    if (input_branch < 0 || input_branch >= n) {
        throw std::runtime_error("Noise analysis: input source '" + input_src +
                                 "' has invalid branch index");
    }

    // ---------------------------------------------------------------
    // 3. DC operating point (same as AC analysis)
    // ---------------------------------------------------------------
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
                throw ConvergenceError("Noise analysis: DC operating point failed to converge");
            }
        }
    }

    // ---------------------------------------------------------------
    // 4. Build G and C matrices
    // ---------------------------------------------------------------
    const auto& pattern = ckt.pattern();
    NumericMatrix G(pattern);
    NumericMatrix C(pattern);
    G.clear();
    C.clear();

    for (auto& dev : ckt.devices()) {
        dev->ac_stamp(dc_solution, G, C);
    }

    // ---------------------------------------------------------------
    // 5. Generate frequency points
    // ---------------------------------------------------------------
    auto freqs = generate_frequencies(mode, npoints, fstart, fstop);
    if (freqs.empty()) {
        return NoiseResult{};
    }

    // ---------------------------------------------------------------
    // 6. Build 2n x 2n sparsity pattern (same as AC)
    //    Layout: rows/cols 0..n-1 are real part, n..2n-1 are imaginary part
    //    [Re(Y)  -Im(Y)] [Re(x)]   [Re(b)]
    //    [Im(Y)   Re(Y)] [Im(x)] = [Im(b)]
    // ---------------------------------------------------------------
    const int32_t n2 = 2 * n;
    SparsityBuilder builder(n2);

    const auto& entries = pattern.entries();
    for (const auto& [r, c] : entries) {
        builder.add(r, c);
        builder.add(r + n, c + n);
        builder.add(r, c + n);
        builder.add(r + n, c);
    }

    auto pattern_2n = builder.build();
    NumericMatrix mat_2n(pattern_2n);

    // We need two solvers:
    //   1. For the adjoint problem: Y^T * adj = e_out
    //   2. For the gain computation: Y * x = e_input
    // Since Y^T has a different sparsity structure from Y, we build a
    // separate transposed pattern.

    // Build transposed pattern for Y^T
    SparsityBuilder builder_t(n2);
    for (const auto& [r, c] : entries) {
        // Transpose: swap row and col
        builder_t.add(c, r);
        builder_t.add(c + n, r + n);
        builder_t.add(c + n, r);
        builder_t.add(c, r + n);
    }
    auto pattern_2n_t = builder_t.build();
    NumericMatrix mat_2n_t(pattern_2n_t);

    // Symbolic factorization once for each
    KLUSolver gain_solver;    // for Y * x = e_input
    gain_solver.symbolic(pattern_2n);

    KLUSolver adj_solver;     // for Y^T * adj = e_out
    adj_solver.symbolic(pattern_2n_t);

    // ---------------------------------------------------------------
    // 7. Prepare result
    // ---------------------------------------------------------------
    NoiseResult noise_result;
    noise_result.frequency = freqs;
    noise_result.output_noise_density.resize(freqs.size(), 0.0);
    noise_result.input_noise_density.resize(freqs.size(), 0.0);

    // Initialize per-device breakdown
    for (const auto& dev : ckt.devices()) {
        auto sources = dev->noise_sources(1.0, dc_solution);
        if (!sources.empty()) {
            noise_result.device_noise[to_lower(dev->name())].resize(freqs.size(), 0.0);
        }
    }

    // ---------------------------------------------------------------
    // 8. Frequency sweep
    // ---------------------------------------------------------------
    for (size_t fi = 0; fi < freqs.size(); ++fi) {
        double omega = 2.0 * M_PI * freqs[fi];

        // Build Y matrix (2n x 2n)
        mat_2n.clear();
        for (const auto& [r, c] : entries) {
            double g_val = G.value(pattern.offset(r, c));
            double c_val = C.value(pattern.offset(r, c));

            double re_y = g_val;
            double im_y = omega * c_val;

            // Top-left: Re(Y)
            mat_2n.add(pattern_2n.offset(r, c), re_y);
            // Bottom-right: Re(Y)
            mat_2n.add(pattern_2n.offset(r + n, c + n), re_y);
            // Top-right: -Im(Y)
            mat_2n.add(pattern_2n.offset(r, c + n), -im_y);
            // Bottom-left: Im(Y)
            mat_2n.add(pattern_2n.offset(r + n, c), im_y);
        }

        // Build Y^T matrix (transpose of Y)
        mat_2n_t.clear();
        for (const auto& [r, c] : entries) {
            double g_val = G.value(pattern.offset(r, c));
            double c_val = C.value(pattern.offset(r, c));

            double re_y = g_val;
            double im_y = omega * c_val;

            // Y^T: swap (r,c) -> (c,r)
            // Top-left: Re(Y^T)
            mat_2n_t.add(pattern_2n_t.offset(c, r), re_y);
            // Bottom-right: Re(Y^T)
            mat_2n_t.add(pattern_2n_t.offset(c + n, r + n), re_y);
            // Top-right: -Im(Y^T)
            mat_2n_t.add(pattern_2n_t.offset(c, r + n), -im_y);
            // Bottom-left: Im(Y^T)
            mat_2n_t.add(pattern_2n_t.offset(c + n, r), im_y);
        }

        // ---- Solve adjoint: Y^T * adj = e_out ----
        std::vector<double> rhs_adj(n2, 0.0);
        rhs_adj[out_idx] = 1.0;  // unit real excitation at output node

        if (fi == 0) {
            adj_solver.numeric(pattern_2n_t, mat_2n_t);
        } else {
            adj_solver.refactorize(mat_2n_t);
        }
        adj_solver.solve(rhs_adj);
        // rhs_adj now contains adj: rhs_adj[i] = Re(adj[i]), rhs_adj[i+n] = Im(adj[i])

        // ---- Compute gain: Y * x = e_input ----
        // The input excitation is a unit voltage at the input source's branch equation
        std::vector<double> rhs_gain(n2, 0.0);
        rhs_gain[input_branch] = 1.0;  // unit real excitation at input source branch

        if (fi == 0) {
            gain_solver.numeric(pattern_2n, mat_2n);
        } else {
            gain_solver.refactorize(mat_2n);
        }
        gain_solver.solve(rhs_gain);

        // Gain from input source to output node
        double gain_re = rhs_gain[out_idx];
        double gain_im = rhs_gain[out_idx + n];
        double gain_sq = gain_re * gain_re + gain_im * gain_im;

        // ---- Accumulate noise from all devices ----
        double total_output_noise = 0.0;

        for (const auto& dev : ckt.devices()) {
            auto sources = dev->noise_sources(freqs[fi], dc_solution);
            if (sources.empty()) continue;

            double device_contribution = 0.0;
            for (const auto& ns : sources) {
                // Adjoint values at the two noise source nodes
                double adj_i_re = 0.0, adj_i_im = 0.0;
                double adj_j_re = 0.0, adj_j_im = 0.0;

                if (ns.node_i >= 0 && ns.node_i < n) {
                    adj_i_re = rhs_adj[ns.node_i];
                    adj_i_im = rhs_adj[ns.node_i + n];
                }
                if (ns.node_j >= 0 && ns.node_j < n) {
                    adj_j_re = rhs_adj[ns.node_j];
                    adj_j_im = rhs_adj[ns.node_j + n];
                }

                // |adj[i] - adj[j]|^2
                double diff_re = adj_i_re - adj_j_re;
                double diff_im = adj_i_im - adj_j_im;
                double transfer_sq = diff_re * diff_re + diff_im * diff_im;

                // Output noise contribution: S * |H|^2
                device_contribution += ns.spectral_density * transfer_sq;
            }

            total_output_noise += device_contribution;

            // Per-device breakdown
            std::string dev_key = to_lower(dev->name());
            auto it = noise_result.device_noise.find(dev_key);
            if (it != noise_result.device_noise.end()) {
                it->second[fi] = device_contribution;
            }
        }

        noise_result.output_noise_density[fi] = total_output_noise;

        // Input-referred noise: output_noise / |gain|^2
        if (gain_sq > 0.0) {
            noise_result.input_noise_density[fi] = total_output_noise / gain_sq;
        } else {
            noise_result.input_noise_density[fi] = 0.0;
        }
    }

    return noise_result;
}

} // namespace neospice
