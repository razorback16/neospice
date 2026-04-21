#include "devices/cccs_nonlinear.hpp"
#include <cmath>
#include <functional>

namespace neospice {

NonlinearCCCS::NonlinearCCCS(std::string name,
                              int32_t node_pos, int32_t node_neg,
                              std::vector<const VSource*> vsenses,
                              std::vector<double> coefficients)
    : Device(std::move(name))
    , np_(node_pos)
    , nn_(node_neg)
    , vsenses_(std::move(vsenses))
    , coeffs_(std::move(coefficients))
{}

// ---------------------------------------------------------------------------
// Polynomial evaluation — identical to NonlinearVCCS::eval_poly
// ---------------------------------------------------------------------------
double NonlinearCCCS::eval_poly(const std::vector<double>& ctrl_i,
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

    // Multi-dimensional POLY
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

void NonlinearCCCS::stamp_pattern(SparsityBuilder& builder) const {
    for (const auto* vs : vsenses_) {
        int32_t sb = vs->branch_index();
        // sense_branch is always >= 0 (branch variable, never ground)
        if (np_ >= 0) builder.add(np_, sb);
        if (nn_ >= 0) builder.add(nn_, sb);
    }
}

void NonlinearCCCS::assign_offsets(const SparsityPattern& pattern) {
    off_np_sense_.resize(vsenses_.size());
    off_nn_sense_.resize(vsenses_.size());
    for (size_t k = 0; k < vsenses_.size(); ++k) {
        int32_t sb = vsenses_[k]->branch_index();
        off_np_sense_[k] = (np_ >= 0) ? pattern.offset(np_, sb) : -1;
        off_nn_sense_[k] = (nn_ >= 0) ? pattern.offset(nn_, sb) : -1;
    }
}

void NonlinearCCCS::evaluate(const std::vector<double>& voltages,
                              NumericMatrix& mat, std::vector<double>& rhs) {
    // Compute sensing currents
    std::vector<double> ctrl_i(vsenses_.size());
    for (size_t k = 0; k < vsenses_.size(); ++k) {
        ctrl_i[k] = voltages[vsenses_[k]->branch_index()];
    }

    std::vector<double> derivs;
    double f_val = eval_poly(ctrl_i, derivs);

    // SPICE convention: I = f(Ic) leaves N+ (np).
    // Jacobian: mat[np, sense_branch_k] += df/dIk
    //           mat[nn, sense_branch_k] -= df/dIk
    for (size_t k = 0; k < vsenses_.size(); ++k) {
        add_if_valid(mat, off_np_sense_[k],  derivs[k]);
        add_if_valid(mat, off_nn_sense_[k], -derivs[k]);
    }

    // Newton companion: rhs[np] = -(f(I0) - sum(df/dIk * Ik0))
    double companion = f_val;
    for (size_t k = 0; k < vsenses_.size(); ++k) {
        companion -= derivs[k] * ctrl_i[k];
    }
    add_rhs_if_valid(rhs, np_, -companion);
    add_rhs_if_valid(rhs, nn_,  companion);
}

void NonlinearCCCS::ac_stamp(const std::vector<double>& voltages,
                              NumericMatrix& G, NumericMatrix& /*C*/) {
    std::vector<double> ctrl_i(vsenses_.size());
    for (size_t k = 0; k < vsenses_.size(); ++k) {
        ctrl_i[k] = voltages[vsenses_[k]->branch_index()];
    }

    std::vector<double> derivs;
    eval_poly(ctrl_i, derivs);

    for (size_t k = 0; k < vsenses_.size(); ++k) {
        add_if_valid(G, off_np_sense_[k],  derivs[k]);
        add_if_valid(G, off_nn_sense_[k], -derivs[k]);
    }
}

} // namespace neospice
