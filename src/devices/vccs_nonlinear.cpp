#include "devices/vccs_nonlinear.hpp"
#include <cmath>
#include <algorithm>
#include <functional>

namespace neospice {

// ===========================================================================
// NonlinearVCCS — Polynomial form
// ===========================================================================

NonlinearVCCS::NonlinearVCCS(std::string name,
                              int32_t node_pos, int32_t node_neg,
                              std::vector<CtrlPair> ctrl_pairs,
                              std::vector<double> coefficients)
    : Device(std::move(name))
    , np_(node_pos)
    , nn_(node_neg)
    , ctrl_pairs_(std::move(ctrl_pairs))
    , coeffs_(std::move(coefficients))
{}

// ---------------------------------------------------------------------------
// Polynomial evaluation — identical logic to NonlinearVCVS::eval_poly
// (duplicated to avoid cross-class dependency on a non-virtual method)
// ---------------------------------------------------------------------------
double NonlinearVCCS::eval_poly(const std::vector<double>& ctrl_v,
                                 std::vector<double>& derivs) const {
    const size_t ndim = ctrl_pairs_.size();
    derivs.assign(ndim, 0.0);

    if (coeffs_.empty()) return 0.0;

    if (ndim == 1) {
        double v = ctrl_v[0];
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

    // Multi-dimensional
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
                mono *= std::pow(ctrl_v[k], static_cast<double>(ev[k]));
            }
            val += mono;

            for (size_t k = 0; k < ndim; ++k) {
                if (ev[k] == 0) continue;
                double dterm = c * static_cast<double>(ev[k]);
                for (size_t j = 0; j < ndim; ++j) {
                    int exp_j = (j == k) ? ev[j] - 1 : ev[j];
                    dterm *= std::pow(ctrl_v[j], static_cast<double>(exp_j));
                }
                derivs[k] += dterm;
            }
        }
    }

    return val;
}

void NonlinearVCCS::stamp_pattern(SparsityBuilder& builder) const {
    for (const auto& cp : ctrl_pairs_) {
        stamp_if_not_ground(builder, np_, cp.pos);
        stamp_if_not_ground(builder, np_, cp.neg);
        stamp_if_not_ground(builder, nn_, cp.pos);
        stamp_if_not_ground(builder, nn_, cp.neg);
    }
}

void NonlinearVCCS::assign_offsets(const SparsityPattern& pattern) {
    off_np_cp_.resize(ctrl_pairs_.size());
    off_np_cn_.resize(ctrl_pairs_.size());
    off_nn_cp_.resize(ctrl_pairs_.size());
    off_nn_cn_.resize(ctrl_pairs_.size());
    for (size_t k = 0; k < ctrl_pairs_.size(); ++k) {
        off_np_cp_[k] = offset_if_not_ground(pattern, np_, ctrl_pairs_[k].pos);
        off_np_cn_[k] = offset_if_not_ground(pattern, np_, ctrl_pairs_[k].neg);
        off_nn_cp_[k] = offset_if_not_ground(pattern, nn_, ctrl_pairs_[k].pos);
        off_nn_cn_[k] = offset_if_not_ground(pattern, nn_, ctrl_pairs_[k].neg);
    }
}

void NonlinearVCCS::evaluate(const std::vector<double>& voltages,
                              NumericMatrix& mat, std::vector<double>& rhs) {
    std::vector<double> ctrl_v(ctrl_pairs_.size());
    for (size_t k = 0; k < ctrl_pairs_.size(); ++k) {
        double vp = (ctrl_pairs_[k].pos >= 0) ? voltages[ctrl_pairs_[k].pos] : 0.0;
        double vn = (ctrl_pairs_[k].neg >= 0) ? voltages[ctrl_pairs_[k].neg] : 0.0;
        ctrl_v[k] = vp - vn;
    }

    std::vector<double> derivs;
    double f_val = eval_poly(ctrl_v, derivs);

    // Jacobian stamps:
    //   mat[np, cpk] += -df/dVk  (Jacobian of KCL at np w.r.t. V(cpk))
    //   mat[np, cnk] +=  df/dVk
    //   mat[nn, cpk] +=  df/dVk  (Jacobian of KCL at nn)
    //   mat[nn, cnk] += -df/dVk
    for (size_t k = 0; k < ctrl_pairs_.size(); ++k) {
        add_if_valid(mat, off_np_cp_[k], -derivs[k]);
        add_if_valid(mat, off_np_cn_[k],  derivs[k]);
        add_if_valid(mat, off_nn_cp_[k],  derivs[k]);
        add_if_valid(mat, off_nn_cn_[k], -derivs[k]);
    }

    // Newton companion RHS:
    // companion = f(Vc_k) - sum_k(df/dVk * Vk_k)
    //
    // At node np (KCL: current leaving np via VCCS = f(Vc)):
    //   Linearized equation: -sum_k(df/dVk * V(cpk)) + sum_k(df/dVk * V(cnk)) = rhs[np]
    //   rhs[np] = f(Vc_k) - sum_k(df/dVk * Vk_k) = companion
    //
    // At node nn (KCL: current entering nn via VCCS = f(Vc)):
    //   rhs[nn] = -(f(Vc_k) - sum_k(df/dVk * Vk_k)) = -companion
    double companion = f_val;
    for (size_t k = 0; k < ctrl_pairs_.size(); ++k) {
        companion -= derivs[k] * ctrl_v[k];
    }
    add_rhs_if_valid(rhs, np_,  companion);
    add_rhs_if_valid(rhs, nn_, -companion);
}

void NonlinearVCCS::ac_stamp(const std::vector<double>& voltages,
                              NumericMatrix& G, NumericMatrix& /*C*/) {
    std::vector<double> ctrl_v(ctrl_pairs_.size());
    for (size_t k = 0; k < ctrl_pairs_.size(); ++k) {
        double vp = (ctrl_pairs_[k].pos >= 0) ? voltages[ctrl_pairs_[k].pos] : 0.0;
        double vn = (ctrl_pairs_[k].neg >= 0) ? voltages[ctrl_pairs_[k].neg] : 0.0;
        ctrl_v[k] = vp - vn;
    }

    std::vector<double> derivs;
    eval_poly(ctrl_v, derivs);

    for (size_t k = 0; k < ctrl_pairs_.size(); ++k) {
        add_if_valid(G, off_np_cp_[k], -derivs[k]);
        add_if_valid(G, off_np_cn_[k],  derivs[k]);
        add_if_valid(G, off_nn_cp_[k],  derivs[k]);
        add_if_valid(G, off_nn_cn_[k], -derivs[k]);
    }
}

// ===========================================================================
// TableVCCS — Piecewise-linear table form
// ===========================================================================

TableVCCS::TableVCCS(std::string name,
                     int32_t node_pos, int32_t node_neg,
                     int32_t ctrl_pos, int32_t ctrl_neg,
                     std::vector<TablePoint> table_points)
    : Device(std::move(name))
    , np_(node_pos)
    , nn_(node_neg)
    , ncp_(ctrl_pos)
    , ncn_(ctrl_neg)
    , table_(std::move(table_points))
{
    std::sort(table_.begin(), table_.end(),
              [](const TablePoint& a, const TablePoint& b) { return a.x < b.x; });
}

double TableVCCS::interp(double x, double& slope) const {
    if (table_.empty()) { slope = 0.0; return 0.0; }
    if (table_.size() == 1) { slope = 0.0; return table_[0].y; }

    if (x <= table_.front().x) {
        slope = 0.0;
        return table_.front().y;
    }
    if (x >= table_.back().x) {
        slope = 0.0;
        return table_.back().y;
    }

    size_t lo = 0, hi = table_.size() - 1;
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if (table_[mid].x <= x) lo = mid;
        else hi = mid;
    }
    double dx = table_[hi].x - table_[lo].x;
    if (dx == 0.0) { slope = 0.0; return table_[lo].y; }
    slope = (table_[hi].y - table_[lo].y) / dx;
    double t = (x - table_[lo].x) / dx;
    return table_[lo].y + t * (table_[hi].y - table_[lo].y);
}

void TableVCCS::stamp_pattern(SparsityBuilder& builder) const {
    stamp_if_not_ground(builder, np_, ncp_);
    stamp_if_not_ground(builder, np_, ncn_);
    stamp_if_not_ground(builder, nn_, ncp_);
    stamp_if_not_ground(builder, nn_, ncn_);
}

void TableVCCS::assign_offsets(const SparsityPattern& pattern) {
    off_np_ncp_ = offset_if_not_ground(pattern, np_, ncp_);
    off_np_ncn_ = offset_if_not_ground(pattern, np_, ncn_);
    off_nn_ncp_ = offset_if_not_ground(pattern, nn_, ncp_);
    off_nn_ncn_ = offset_if_not_ground(pattern, nn_, ncn_);
}

void TableVCCS::evaluate(const std::vector<double>& voltages,
                          NumericMatrix& mat, std::vector<double>& rhs) {
    double vp = (ncp_ >= 0) ? voltages[ncp_] : 0.0;
    double vn = (ncn_ >= 0) ? voltages[ncn_] : 0.0;
    double vc = vp - vn;

    double slope = 0.0;
    double f_val = interp(vc, slope);

    // Jacobian
    add_if_valid(mat, off_np_ncp_, -slope);
    add_if_valid(mat, off_np_ncn_,  slope);
    add_if_valid(mat, off_nn_ncp_,  slope);
    add_if_valid(mat, off_nn_ncn_, -slope);

    // Newton companion: rhs[np] = f(Vc) - slope*Vc, rhs[nn] = -(f(Vc) - slope*Vc)
    double companion = f_val - slope * vc;
    add_rhs_if_valid(rhs, np_,  companion);
    add_rhs_if_valid(rhs, nn_, -companion);
}

void TableVCCS::ac_stamp(const std::vector<double>& voltages,
                          NumericMatrix& G, NumericMatrix& /*C*/) {
    double vp = (ncp_ >= 0) ? voltages[ncp_] : 0.0;
    double vn = (ncn_ >= 0) ? voltages[ncn_] : 0.0;
    double vc = vp - vn;

    double slope = 0.0;
    interp(vc, slope);

    add_if_valid(G, off_np_ncp_, -slope);
    add_if_valid(G, off_np_ncn_,  slope);
    add_if_valid(G, off_nn_ncp_,  slope);
    add_if_valid(G, off_nn_ncn_, -slope);
}

} // namespace neospice
