#include "core/ac.hpp"
#include "core/dc.hpp"
#include "core/freq_utils.hpp"
#include "core/newton.hpp"
#include "core/convergence.hpp"
#include "core/neo_solver.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace neospice {

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

static std::string make_branch_key(const std::string& dname) {
    std::string lower = to_lower(dname);
    auto dot = lower.rfind('.');
    if (dot != std::string::npos && dot + 1 < lower.size()) {
        char type_letter = lower[dot + 1];
        return "i(" + std::string(1, type_letter) + "." + lower + ")";
    }
    return "i(" + lower + ")";
}

static SimOptions direct_attempt_options(const SimOptions& opts) {
    SimOptions direct_opts = opts;
    direct_opts.max_iter = opts.itl1;
    return direct_opts;
}

static bool can_use_zero_ac_operating_point(const Circuit& ckt) {
    for (const auto& dev : ckt.devices()) {
        const std::string type = dev->device_type();
        if (type == "D" || type == "Q" || type == "J" || type == "M" ||
            type == "S" || type == "W" || type == "B") {
            return false;
        }
    }
    return true;
}

ACResult solve_ac(Circuit& ckt, ACMode mode,
                  int npoints, double fstart, double fstop) {
    auto t_start = std::chrono::steady_clock::now();
    const int32_t n = ckt.num_vars();
    const int32_t num_nodes = ckt.num_nodes();

    // 1. DC operating point.  If a same-circuit .op just ran, reuse its full
    // MNA vector and only do the small-signal initialization below.
    std::vector<double> dc_solution;
    if (const auto* cached_op = ckt.operating_point();
        cached_op && static_cast<int32_t>(cached_op->size()) == n) {
        dc_solution = *cached_op;
    } else {
        try {
            auto dc = solve_dc(ckt);
            if (!dc.status.converged) {
                ACResult fail_result;
                fail_result.status = dc.status;
                return fail_result;
            }
            const auto* cached = ckt.operating_point();
            if (!cached || static_cast<int32_t>(cached->size()) != n)
                throw std::logic_error("AC analysis: DC operating point cache missing");
            dc_solution = *cached;
        } catch (const SimulationError& e) {
            if (!can_use_zero_ac_operating_point(ckt))
                throw;
            dc_solution.assign(n, 0.0);
            ckt.set_operating_point(dc_solution);
        }
    }
    ckt.integrator_ctx.options = &ckt.options;

    // 2. MODEINITSMSIG pass — compute small-signal parameters (capacitances,
    //    transconductances) at the DC operating point.  ngspice runs this
    //    extra evaluation after DC convergence so the state vectors contain
    //    the small-signal values that ac_stamp() reads.
    {
        constexpr int MODEAC_BIT        = 0x2;
        constexpr int MODEINITSMSIG_BIT = 0x800;
        ckt.integrator_ctx.mode = MODEAC_BIT | MODEINITSMSIG_BIT;

        NumericMatrix smsig_mat(ckt.pattern());
        smsig_mat.clear();
        std::vector<double> smsig_rhs(ckt.num_vars(), 0.0);

        struct IntegratorCtxGuard {
            IntegratorCtxGuard(const IntegratorCtx& c) { tls_integrator_ctx = &c; }
            ~IntegratorCtxGuard()                      { tls_integrator_ctx = nullptr; }
        } guard(ckt.integrator_ctx);
        for (auto& dev : ckt.devices()) {
            dev->evaluate(dc_solution, smsig_mat, smsig_rhs);
        }
    }

    // 3. Build G and C matrices using the DC sparsity pattern
    const auto& pattern = ckt.pattern();
    NumericMatrix G(pattern);
    NumericMatrix C(pattern);
    G.clear();
    C.clear();

    for (auto& dev : ckt.devices()) {
        dev->ac_stamp(dc_solution, G, C);
    }

    // 4. Build AC excitation vector (complex RHS)
    std::vector<std::complex<double>> ac_rhs(n, {0.0, 0.0});
    for (auto& dev : ckt.devices()) {
        dev->apply_ac_excitation(ac_rhs, n);
    }

    // 5. Generate frequency points
    auto freqs = generate_frequencies(mode, npoints, fstart, fstop);
    if (freqs.empty()) {
        return ACResult{};
    }

    // 6. Pre-cache G/C value arrays for direct indexing
    const int32_t nnz = pattern.nnz();
    std::vector<double> g_vals(nnz);
    std::vector<double> c_vals(nnz);
    for (int32_t k = 0; k < nnz; ++k) {
        g_vals[k] = G.data()[k];
        c_vals[k] = C.data()[k];
    }

    // 7. Symbolic factorization on n×n pattern (reuse DC pattern)
    auto ac_solver = std::make_unique<NeoSolver>();
    ac_solver->symbolic(pattern);

    // 8. Pre-compute result extraction indices (outside frequency loop)
    struct VoltageSlot {
        std::string key;
        int32_t var_idx;
    };
    struct CurrentSlot {
        std::string key;
        int32_t branch_idx;
    };
    std::vector<VoltageSlot> voltage_slots;
    voltage_slots.reserve(num_nodes);
    for (int32_t i = 0; i < num_nodes; ++i) {
        if (ckt.is_internal_node(i)) continue;
        voltage_slots.push_back({"v(" + to_lower(ckt.node_name(i)) + ")", i});
    }
    std::vector<CurrentSlot> current_slots;
    auto add_current = [&](int32_t br, const std::string& dname) {
        if (br >= 0 && br < n)
            current_slots.push_back({make_branch_key(dname), br});
    };
    for (auto& dev : ckt.devices()) {
        add_current(dev->branch_index(), dev->name());
    }

    // Prepare result + cache direct pointers for zero-lookup frequency loop
    ACResult ac_result;
    ac_result.frequency = freqs;
    for (auto& vs : voltage_slots)
        ac_result.voltages[vs.key].resize(freqs.size());
    for (auto& cs : current_slots)
        ac_result.currents[cs.key].resize(freqs.size());

    std::vector<std::complex<double>*> v_ptrs, c_ptrs;
    for (auto& vs : voltage_slots)
        v_ptrs.push_back(ac_result.voltages[vs.key].data());
    for (auto& cs : current_slots)
        c_ptrs.push_back(ac_result.currents[cs.key].data());

    // Initialize dense arrays for handle-based access
    ac_result.voltages_dense.resize(num_nodes, std::vector<std::complex<double>>(freqs.size()));
    ac_result.currents_dense.resize(ckt.devices().size(), std::vector<std::complex<double>>(freqs.size()));

    // 9. Complex RHS template (allocated once, copied per frequency)
    std::vector<double> rhs_z(2 * n, 0.0);
    for (int32_t i = 0; i < n; ++i) {
        rhs_z[2 * i]     = ac_rhs[i].real();
        rhs_z[2 * i + 1] = ac_rhs[i].imag();
    }
    const std::vector<double> rhs_z_template = rhs_z;

    // 10. Complex Ax array: 2*nnz doubles (interleaved real,imag per NNZ in CSC order)
    std::vector<double> ax(2 * nnz);

    // 11. Frequency sweep
    for (size_t fi = 0; fi < freqs.size(); ++fi) {
        double omega = 2.0 * M_PI * freqs[fi];
        ckt.integrator_ctx.ac_freq = freqs[fi];

        for (int32_t k = 0; k < nnz; ++k) {
            ax[2 * k]     = g_vals[k];
            ax[2 * k + 1] = omega * c_vals[k];
        }

        // Per-frequency device stamps (e.g. transmission line cross-port coupling)
        for (auto& dev : ckt.devices()) {
            dev->ac_stamp_freq(omega, ax, nnz, ac_rhs);
        }

        if (fi == 0) {
            ac_solver->numeric_complex(pattern, ax);
        } else {
            ac_solver->refactorize_complex(ax);
        }

        rhs_z = rhs_z_template;
        ac_solver->solve_complex(rhs_z);

        for (std::size_t k = 0; k < voltage_slots.size(); ++k) {
            int32_t i = voltage_slots[k].var_idx;
            v_ptrs[k][fi] = {rhs_z[2*i], rhs_z[2*i+1]};
        }
        for (std::size_t k = 0; k < current_slots.size(); ++k) {
            int32_t br = current_slots[k].branch_idx;
            c_ptrs[k][fi] = {rhs_z[2*br], rhs_z[2*br+1]};
        }

        // Dense node voltage array (ALL nodes, including internal)
        for (int32_t i = 0; i < num_nodes; ++i)
            ac_result.voltages_dense[i][fi] = {rhs_z[2*i], rhs_z[2*i+1]};
        // Dense branch current array (indexed by device ordinal)
        for (std::size_t d = 0; d < ckt.devices().size(); ++d) {
            int32_t br = ckt.devices()[d]->branch_index();
            if (br >= 0 && br < n)
                ac_result.currents_dense[d][fi] = {rhs_z[2*br], rhs_z[2*br+1]};
        }
    }

    auto t_end = std::chrono::steady_clock::now();
    ac_result.status.converged = true;
    ac_result.status.elapsed_seconds = std::chrono::duration<double>(t_end - t_start).count();

    return ac_result;
}

ACResult solve_ac(Circuit& ckt, ACMode mode,
                  int npoints, double fstart, double fstop,
                  const ACOptions& opts) {
    if (opts.op_from) {
        for (const auto& [key, val] : opts.op_from->node_voltages) {
            if (key.size() > 3 && key.front() == 'v' && key[1] == '(') {
                std::string node_name = key.substr(2, key.size() - 3);
                int32_t idx = ckt.node_index(node_name);
                if (idx >= 0) {
                    ckt.nodeset[NodeId{idx}] = val;
                }
            }
        }
    }
    return solve_ac(ckt, mode, npoints, fstart, fstop);
}

} // namespace neospice
