#include "devices/ccvs_nonlinear.hpp"
#include "core/circuit.hpp"   // tls_integrator_ctx
#include <stdexcept>
#include <cmath>
#include <functional>

namespace neospice {

NonlinearCCVS::NonlinearCCVS(std::string name,
                              int32_t node_pos, int32_t node_neg,
                              std::vector<const VSource*> vsenses,
                              std::vector<double> coefficients)
    : Device(std::move(name))
    , np_(node_pos)
    , nn_(node_neg)
    , vsenses_(std::move(vsenses))
    , coeffs_(std::move(coefficients))
{}

void NonlinearCCVS::set_branch_index(int32_t idx) {
    branch_idx_ = idx;
}

std::vector<std::string> NonlinearCCVS::output_currents() const {
    return { "I(" + name_ + ")" };
}

// ---------------------------------------------------------------------------
// Polynomial evaluation — identical to NonlinearVCVS::eval_poly
// ---------------------------------------------------------------------------
double NonlinearCCVS::eval_poly(const std::vector<double>& ctrl_i,
                                 std::vector<double>& derivs) const {
    const size_t ndim = vsenses_.size();
    derivs.assign(ndim, 0.0);

    if (coeffs_.empty()) return 0.0;

    if (ndim == 1) {
        double v = ctrl_i[0];
        double val = 0.0;
        double deriv = 0.0;
        for (size_t i = 0; i < coeffs_.size(); ++i) {
            val += coeffs_[i] * std::pow(v, static_cast<double>(i));
        }
        for (size_t i = 1; i < coeffs_.size(); ++i) {
            deriv += static_cast<double>(i) * coeffs_[i]
                     * std::pow(v, static_cast<double>(i) - 1.0);
        }
        derivs[0] = deriv;
        return val;
    }

    // Multi-dimensional POLY: enumerate all monomials in SPICE order.
    double val = 0.0;
    size_t coeff_idx = 0;

    for (int d = 0; coeff_idx < coeffs_.size(); ++d) {
        std::vector<int> exp(ndim, 0);
        std::vector<std::vector<int>> exps;

        std::function<void(int, int, std::vector<int>&)> gen =
            [&](int rem, int dim, std::vector<int>& cur) {
                if (dim == static_cast<int>(ndim) - 1) {
                    cur[dim] = rem;
                    exps.push_back(cur);
                    return;
                }
                for (int e = rem; e >= 0; --e) {
                    cur[dim] = e;
                    gen(rem - e, dim + 1, cur);
                }
            };
        gen(d, 0, exp);

        for (const auto& ev : exps) {
            if (coeff_idx >= coeffs_.size()) break;
            double c = coeffs_[coeff_idx++];
            if (c == 0.0) continue;

            double mono = c;
            for (size_t k = 0; k < ndim; ++k) {
                mono *= std::pow(ctrl_i[k], static_cast<double>(ev[k]));
            }
            val += mono;

            for (size_t k = 0; k < ndim; ++k) {
                if (ev[k] == 0) continue;
                double dterm = c * static_cast<double>(ev[k]);
                for (size_t j = 0; j < ndim; ++j) {
                    int exp_j = (j == k) ? ev[j] - 1 : ev[j];
                    dterm *= std::pow(ctrl_i[j], static_cast<double>(exp_j));
                }
                derivs[k] += dterm;
            }
        }
    }

    return val;
}

void NonlinearCCVS::stamp_pattern(SparsityBuilder& builder) const {
    if (branch_idx_ < 0)
        throw std::logic_error("NonlinearCCVS::stamp_pattern called before set_branch_index");

    // KCL rows: output nodes couple to branch current
    stamp_if_not_ground(builder, np_, branch_idx_);
    stamp_if_not_ground(builder, nn_, branch_idx_);

    // Branch equation row: branch couples to output nodes and all sense branches
    stamp_if_not_ground(builder, branch_idx_, np_);
    stamp_if_not_ground(builder, branch_idx_, nn_);
    for (const auto* vs : vsenses_) {
        int32_t sb = vs->branch_index();
        // sense_branch is always >= 0 (branch variable, never ground)
        builder.add(branch_idx_, sb);
    }
}

void NonlinearCCVS::assign_offsets(const SparsityPattern& pattern) {
    off_np_branch_ = offset_if_not_ground(pattern, np_,         branch_idx_);
    off_nn_branch_ = offset_if_not_ground(pattern, nn_,         branch_idx_);
    off_branch_np_ = offset_if_not_ground(pattern, branch_idx_, np_);
    off_branch_nn_ = offset_if_not_ground(pattern, branch_idx_, nn_);

    off_branch_sense_.resize(vsenses_.size());
    for (size_t k = 0; k < vsenses_.size(); ++k) {
        int32_t sb = vsenses_[k]->branch_index();
        off_branch_sense_[k] = pattern.offset(branch_idx_, sb);
    }
}

void NonlinearCCVS::evaluate(const std::vector<double>& voltages,
                              NumericMatrix& mat, std::vector<double>& rhs) {
    // Compute sensing currents Ik = I(Vsk) = voltages[sense_branch_k]
    std::vector<double> ctrl_i(vsenses_.size());
    for (size_t k = 0; k < vsenses_.size(); ++k) {
        ctrl_i[k] = voltages[vsenses_[k]->branch_index()];
    }

    std::vector<double> derivs;
    double f_val = eval_poly(ctrl_i, derivs);

    // Scale by dep_src_fact for gain stepping convergence aid
    double dsf = 1.0;
    if (tls_integrator_ctx && tls_integrator_ctx->options)
        dsf = tls_integrator_ctx->options->dep_src_fact;

    // KCL at output nodes: +I_branch into np, -I_branch out of nn
    add_if_valid(mat, off_np_branch_,  1.0);
    add_if_valid(mat, off_nn_branch_, -1.0);

    // Branch equation (Jacobian part):
    //   V(np) - V(nn) - f(I)*dsf = 0
    //   mat[branch, np] += 1, mat[branch, nn] -= 1,
    //   mat[branch, sense_branch_k] -= df/dIk * dsf
    add_if_valid(mat, off_branch_np_,  1.0);
    add_if_valid(mat, off_branch_nn_, -1.0);
    for (size_t k = 0; k < vsenses_.size(); ++k) {
        add_if_valid(mat, off_branch_sense_[k], -derivs[k] * dsf);
    }

    // RHS: Newton companion for branch equation
    // rhs[branch] = f(I0)*dsf - sum_k(df/dIk * dsf * Ik0)
    double rhs_val = f_val * dsf;
    for (size_t k = 0; k < vsenses_.size(); ++k) {
        rhs_val -= derivs[k] * dsf * ctrl_i[k];
    }
    add_rhs_if_valid(rhs, branch_idx_, rhs_val);
}

void NonlinearCCVS::ac_stamp(const std::vector<double>& voltages,
                              NumericMatrix& G, NumericMatrix& /*C*/) {
    // For AC analysis, linearize about the DC operating point.
    std::vector<double> ctrl_i(vsenses_.size());
    for (size_t k = 0; k < vsenses_.size(); ++k) {
        ctrl_i[k] = voltages[vsenses_[k]->branch_index()];
    }

    std::vector<double> derivs;
    eval_poly(ctrl_i, derivs);

    // AC small-signal stamp
    add_if_valid(G, off_np_branch_,  1.0);
    add_if_valid(G, off_nn_branch_, -1.0);
    add_if_valid(G, off_branch_np_,  1.0);
    add_if_valid(G, off_branch_nn_, -1.0);
    for (size_t k = 0; k < vsenses_.size(); ++k) {
        add_if_valid(G, off_branch_sense_[k], -derivs[k]);
    }
}

} // namespace neospice
