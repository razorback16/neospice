#include "core/tf.hpp"
#include "core/dc.hpp"
#include "core/newton.hpp"
#include "core/convergence.hpp"
#include "core/klu_solver.hpp"
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include "devices/inductor.hpp"
#include "devices/vcvs.hpp"
#include "devices/ccvs.hpp"
#include "devices/vcvs_nonlinear.hpp"
#include "devices/asrc/asrc_device.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace neospice {

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

TFResult solve_tf(Circuit& ckt, const std::string& output_var,
                  const std::string& input_src) {
    auto t_start = std::chrono::steady_clock::now();
    // ---------------------------------------------------------------
    // 1. DC operating point
    // ---------------------------------------------------------------
    solve_dc(ckt);

    // ---------------------------------------------------------------
    // 2. Re-assemble the Jacobian at the converged operating point
    //    and factor it so we can do two sensitivity solves.
    // ---------------------------------------------------------------
    const int32_t n = ckt.num_vars();
    const int32_t num_nodes = ckt.num_nodes();
    const auto& pattern = ckt.pattern();

    // Get the converged solution by running DC again — but since the
    // circuit is already at the operating point the Newton loop will
    // converge in one iteration.  Instead, we rebuild the Jacobian
    // matrix directly from the converged state.

    // First, collect the converged solution vector.  solve_dc() only
    // returns a DCResult (maps), so we reconstruct the dense vector
    // by running one device evaluation at the OP.

    // A simpler approach: do a fresh DC solve keeping the solver and
    // solution around.  We mirror the core of solve_dc() here.
    std::vector<double> solution(n, 0.0);
    std::vector<char> pinned(n, 0);
    for (auto& [node_idx, value] : ckt.nodeset) {
        if (node_idx >= 0 && node_idx < n) {
            solution[node_idx] = value;
            pinned[node_idx] = 1;
        }
    }
    for (auto& [node_idx, value] : ckt.ic) {
        if (node_idx >= 0 && node_idx < n && !pinned[node_idx]) {
            solution[node_idx] = value;
        }
    }

    KLUSolver solver;
    solver.symbolic(pattern);

    ckt.integrator_ctx.options = &ckt.options;

    constexpr int MODEDCOP_BIT    = 0x10;
    constexpr int MODEINITJCT_BIT = 0x200;
    constexpr int MODEINITFIX_BIT = 0x400;

    // DC operating point — try newton, then gmin stepping, then source stepping
    ckt.integrator_ctx.mode = MODEDCOP_BIT | MODEINITJCT_BIT;
    auto result = newton_solve(ckt, solver, solution, ckt.options);
    if (result.converged) {
        solution = result.solution;
    } else {
        ckt.integrator_ctx.mode = MODEDCOP_BIT | MODEINITFIX_BIT;
        result = gmin_stepping(ckt, solver, solution, ckt.options);
        if (result.converged) {
            solution = result.solution;
        } else {
            ckt.integrator_ctx.mode = MODEDCOP_BIT | MODEINITFIX_BIT;
            result = source_stepping(ckt, solver, solution, ckt.options);
            if (result.converged) {
                solution = result.solution;
            } else {
                ckt.integrator_ctx.mode = MODEDCOP_BIT | MODEINITFIX_BIT;
                result = pseudo_transient(ckt, solver, solution, ckt.options);
                if (result.converged) {
                    solution = result.solution;
                } else {
                    throw ConvergenceError("TF: DC operating point failed to converge");
                }
            }
        }
    }

    // ---------------------------------------------------------------
    // 3. Re-assemble Jacobian at the converged point and factor.
    //    After Newton converges the solver already has a factored
    //    matrix, but Newton may have done a final refactorize.  To be
    //    safe (and match ngspice which just calls SMPsolve on the
    //    existing factorization), we reassemble once more.
    // ---------------------------------------------------------------
    NumericMatrix mat(pattern);
    std::vector<double> rhs(n, 0.0);

    {
        mat.clear();
        std::fill(rhs.begin(), rhs.end(), 0.0);

        struct IntegratorCtxGuard {
            IntegratorCtxGuard(const IntegratorCtx& c) { tls_integrator_ctx = &c; }
            ~IntegratorCtxGuard()                      { tls_integrator_ctx = nullptr; }
        } guard(ckt.integrator_ctx);

        for (auto& dev : ckt.devices()) {
            dev->evaluate(solution, mat, rhs);
        }

        // Add gmin to diagonal of node equations (same as newton.cpp)
        for (int32_t i = 0; i < num_nodes; ++i) {
            try {
                MatrixOffset off = pattern.offset(i, i);
                mat.add(off, ckt.options.gmin);
            } catch (const std::out_of_range&) {}
        }
    }

    // Factor the Jacobian
    solver.numeric(pattern, mat);

    // ---------------------------------------------------------------
    // 4. Find the input source
    // ---------------------------------------------------------------
    std::string src_lower = to_lower(input_src);
    VSource* vsrc = nullptr;
    ISource* isrc = nullptr;

    for (auto& dev : ckt.devices()) {
        if (auto* vs = dynamic_cast<VSource*>(dev.get())) {
            if (to_lower(vs->name()) == src_lower) { vsrc = vs; break; }
        }
        if (auto* is = dynamic_cast<ISource*>(dev.get())) {
            if (to_lower(is->name()) == src_lower) { isrc = is; break; }
        }
    }

    if (!vsrc && !isrc) {
        throw std::invalid_argument("TF: input source '" + input_src + "' not found");
    }

    // ---------------------------------------------------------------
    // 5. Parse the output variable
    // ---------------------------------------------------------------
    std::string out_lower = to_lower(output_var);
    bool out_is_voltage = false;
    bool out_is_current = false;
    int32_t out_pos = -1;   // positive output node (for voltage)
    int32_t out_neg = -1;   // negative output node (for voltage)
    int32_t out_branch = -1;  // branch index (for current)

    if (out_lower.size() > 2 && out_lower[0] == 'v' && out_lower[1] == '(') {
        // Voltage output: v(node) or v(n+,n-)
        out_is_voltage = true;
        std::string inner = out_lower.substr(2, out_lower.size() - 3); // strip v( and )
        auto comma = inner.find(',');
        if (comma != std::string::npos) {
            std::string node_p = inner.substr(0, comma);
            std::string node_n = inner.substr(comma + 1);
            // Trim whitespace
            while (!node_p.empty() && node_p.back() == ' ') node_p.pop_back();
            while (!node_n.empty() && node_n.front() == ' ') node_n.erase(node_n.begin());
            out_pos = ckt.node_index(node_p);
            out_neg = ckt.node_index(node_n);
            if (out_pos < 0 && node_p != "0" && node_p != "gnd") {
                throw std::invalid_argument("TF: output node '" + node_p + "' not found");
            }
            if (out_neg < 0 && node_n != "0" && node_n != "gnd") {
                throw std::invalid_argument("TF: output node '" + node_n + "' not found");
            }
        } else {
            out_pos = ckt.node_index(inner);
            if (out_pos < 0 && inner != "0" && inner != "gnd") {
                throw std::invalid_argument("TF: output node '" + inner + "' not found");
            }
            out_neg = GROUND_INTERNAL;  // single-node: reference to ground
        }
    } else if (out_lower.size() > 2 && out_lower[0] == 'i' && out_lower[1] == '(') {
        // Current output: i(vsource)
        out_is_current = true;
        std::string dev_name = out_lower.substr(2, out_lower.size() - 3);
        // Find the device's branch index
        for (auto& dev : ckt.devices()) {
            if (to_lower(dev->name()) == dev_name) {
                if (auto* vs = dynamic_cast<VSource*>(dev.get())) {
                    out_branch = vs->branch_index();
                } else if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
                    out_branch = ind->branch_index();
                } else if (auto* e = dynamic_cast<VCVS*>(dev.get())) {
                    out_branch = e->branch_index();
                } else if (auto* h = dynamic_cast<CCVS*>(dev.get())) {
                    out_branch = h->branch_index();
                }
                break;
            }
        }
        if (out_branch < 0) {
            throw std::invalid_argument("TF: output current source '" + dev_name + "' not found or has no branch");
        }
    } else {
        throw std::invalid_argument("TF: unrecognized output variable '" + output_var + "'");
    }

    // ---------------------------------------------------------------
    // 6. First solve: transfer function and input impedance
    //    (matches ngspice tfanal.c lines 74-88)
    // ---------------------------------------------------------------
    TFResult tf;
    tf.output_var = out_lower;
    tf.input_src = src_lower;

    std::vector<double> rhs1(n, 0.0);

    if (isrc) {
        // ISource input: inject unit current perturbation at the source nodes
        // ngspice: rhs[node1] -= 1; rhs[node2] += 1
        if (isrc->pos_node() >= 0) rhs1[isrc->pos_node()] -= 1.0;
        if (isrc->neg_node() >= 0) rhs1[isrc->neg_node()] += 1.0;
    } else {
        // VSource input: inject unit voltage excitation at the branch equation
        int32_t insrc = vsrc->branch_index();
        rhs1[insrc] += 1.0;
    }

    solver.solve(rhs1);

    // Transfer function
    if (out_is_voltage) {
        double vp = (out_pos >= 0) ? rhs1[out_pos] : 0.0;
        double vn = (out_neg >= 0) ? rhs1[out_neg] : 0.0;
        tf.transfer_function = vp - vn;
    } else {
        tf.transfer_function = rhs1[out_branch];
    }

    // Input impedance
    if (isrc) {
        // For ISource: Zin = V(node2) - V(node1) (voltage across the source per unit current)
        double vn2 = (isrc->neg_node() >= 0) ? rhs1[isrc->neg_node()] : 0.0;
        double vn1 = (isrc->pos_node() >= 0) ? rhs1[isrc->pos_node()] : 0.0;
        tf.input_impedance = vn2 - vn1;
    } else {
        // For VSource: Zin = -1 / rhs1[branch] (current response to unit voltage)
        int32_t insrc = vsrc->branch_index();
        if (std::abs(rhs1[insrc]) < 1e-20) {
            tf.input_impedance = 1e20;
        } else {
            tf.input_impedance = -1.0 / rhs1[insrc];
        }
    }

    // ---------------------------------------------------------------
    // 7. Second solve: output impedance
    //    (matches ngspice tfanal.c lines 140-157)
    // ---------------------------------------------------------------
    // Check if output source == input source (optimization from ngspice)
    if (out_is_current) {
        std::string out_dev = out_lower.substr(2, out_lower.size() - 3);
        if (out_dev == src_lower) {
            tf.output_impedance = tf.input_impedance;
            auto t_end = std::chrono::steady_clock::now();
            tf.status.converged = true;
            tf.status.elapsed_seconds = std::chrono::duration<double>(t_end - t_start).count();
            return tf;
        }
    }

    std::vector<double> rhs2(n, 0.0);

    if (out_is_voltage) {
        // Inject unit current at output nodes
        // ngspice: rhs[outPos] -= 1; rhs[outNeg] += 1
        if (out_pos >= 0) rhs2[out_pos] -= 1.0;
        if (out_neg >= 0) rhs2[out_neg] += 1.0;
    } else {
        // Output is a branch current: inject unit excitation at that branch
        rhs2[out_branch] += 1.0;
    }

    solver.solve(rhs2);

    // Output impedance
    if (out_is_voltage) {
        // ngspice: outputs[2] = rhs[outNeg] - rhs[outPos]
        double vp = (out_pos >= 0) ? rhs2[out_pos] : 0.0;
        double vn = (out_neg >= 0) ? rhs2[out_neg] : 0.0;
        tf.output_impedance = vn - vp;
    } else {
        if (std::abs(rhs2[out_branch]) < 1e-20) {
            tf.output_impedance = 1e20;
        } else {
            tf.output_impedance = 1.0 / std::max(1e-20, rhs2[out_branch]);
        }
    }

    auto t_end = std::chrono::steady_clock::now();
    tf.status.converged = true;
    tf.status.elapsed_seconds = std::chrono::duration<double>(t_end - t_start).count();
    return tf;
}

} // namespace neospice
