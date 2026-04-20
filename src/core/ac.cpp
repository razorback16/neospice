#include "core/ac.hpp"
#include "core/freq_utils.hpp"
#include "core/newton.hpp"
#include "core/convergence.hpp"
#include "core/klu_solver.hpp"
#include "devices/vsource.hpp"
#include "devices/inductor.hpp"
#include "devices/vcvs.hpp"
#include "devices/ccvs.hpp"
#include "devices/vcvs_nonlinear.hpp"
#include "devices/asrc/asrc_device.hpp"
#include <algorithm>
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

    // Publish SimOptions for BSIM4v7Device (and any future state-storing
    // device) via the same integrator_ctx channel used for CKTmode/ag.
    ckt.integrator_ctx.options = &ckt.options;

    // AC analysis runs a plain DC operating point first — use MODEDCOP (0x10),
    // same as solve_dc().  newton_solve() reads integrator_ctx.mode.
    constexpr int MODEDCOP_BIT    = 0x10;
    constexpr int MODEINITJCT_BIT = 0x200;
    constexpr int MODEINITFIX_BIT = 0x400;

    ckt.integrator_ctx.mode = MODEDCOP_BIT | MODEINITJCT_BIT;
    auto result = newton_solve(ckt, dc_solver, dc_solution, ckt.options);
    if (result.converged) {
        dc_solution = result.solution;
    } else {
        ckt.integrator_ctx.mode = MODEDCOP_BIT | MODEINITFIX_BIT;
        result = gmin_stepping(ckt, dc_solver, dc_solution, ckt.options);
        if (result.converged) {
            dc_solution = result.solution;
        } else {
            ckt.integrator_ctx.mode = MODEDCOP_BIT | MODEINITFIX_BIT;
            result = source_stepping(ckt, dc_solver, dc_solution, ckt.options);
            if (result.converged) {
                dc_solution = result.solution;
            } else {
                throw ConvergenceError("AC analysis: DC operating point failed to converge");
            }
        }
    }

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
        if (auto* vs = dynamic_cast<VSource*>(dev.get())) {
            if (vs->ac_mag() != 0.0) {
                int32_t br = vs->branch_index();
                if (br >= 0 && br < n) {
                    ac_rhs[br] = std::polar(vs->ac_mag(), vs->ac_phase_rad());
                }
            }
        }
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
    KLUSolver ac_solver;
    ac_solver.symbolic(pattern);

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
        if (auto* vs = dynamic_cast<VSource*>(dev.get()))
            add_current(vs->branch_index(), dev->name());
        else if (auto* ind = dynamic_cast<Inductor*>(dev.get()))
            add_current(ind->branch_index(), dev->name());
        else if (auto* e = dynamic_cast<VCVS*>(dev.get()))
            add_current(e->branch_index(), dev->name());
        else if (auto* h = dynamic_cast<CCVS*>(dev.get()))
            add_current(h->branch_index(), dev->name());
        else if (auto* enl = dynamic_cast<NonlinearVCVS*>(dev.get()))
            add_current(enl->branch_index(), dev->name());
        else if (auto* etbl = dynamic_cast<TableVCVS*>(dev.get()))
            add_current(etbl->branch_index(), dev->name());
        else if (auto* bs = dynamic_cast<ASRCDevice*>(dev.get()))
            if (bs->mode() == ASRCDevice::Mode::VOLTAGE)
                add_current(bs->branch_index(), dev->name());
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

        for (int32_t k = 0; k < nnz; ++k) {
            ax[2 * k]     = g_vals[k];
            ax[2 * k + 1] = omega * c_vals[k];
        }

        if (fi == 0) {
            ac_solver.numeric_complex(pattern, ax);
        } else {
            ac_solver.refactorize_complex(ax);
        }

        rhs_z = rhs_z_template;
        ac_solver.solve_complex(rhs_z);

        for (std::size_t k = 0; k < voltage_slots.size(); ++k) {
            int32_t i = voltage_slots[k].var_idx;
            v_ptrs[k][fi] = {rhs_z[2*i], rhs_z[2*i+1]};
        }
        for (std::size_t k = 0; k < current_slots.size(); ++k) {
            int32_t br = current_slots[k].branch_idx;
            c_ptrs[k][fi] = {rhs_z[2*br], rhs_z[2*br+1]};
        }
    }

    return ac_result;
}

} // namespace neospice
