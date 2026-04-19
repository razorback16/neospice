#include "devices/vcvs_nonlinear.hpp"
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <functional>

namespace neospice {

// ===========================================================================
// NonlinearVCVS — Polynomial form
// ===========================================================================

NonlinearVCVS::NonlinearVCVS(std::string name,
                              int32_t node_pos, int32_t node_neg,
                              std::vector<CtrlPair> ctrl_pairs,
                              std::vector<double> coefficients)
    : Device(std::move(name))
    , np_(node_pos)
    , nn_(node_neg)
    , ctrl_pairs_(std::move(ctrl_pairs))
    , coeffs_(std::move(coefficients))
{}

void NonlinearVCVS::set_branch_index(int32_t idx) {
    branch_idx_ = idx;
}

std::vector<std::string> NonlinearVCVS::output_currents() const {
    return { "I(" + name_ + ")" };
}

// ---------------------------------------------------------------------------
// Polynomial evaluation — SPICE multi-variate POLY ordering
//
// For POLY(1):
//   terms: c0, c1*V1, c2*V1^2, c3*V1^3, ...
//
// For POLY(2):
//   terms (Pascal's triangle order): c0, c1*V1, c2*V2,
//         c3*V1^2, c4*V1*V2, c5*V2^2, ...
//
// The iteration below uses exponent vectors in lexicographic reverse order
// (highest power of V1 first within each total-degree group).
// ---------------------------------------------------------------------------
double NonlinearVCVS::eval_poly(const std::vector<double>& ctrl_v,
                                 std::vector<double>& derivs) const {
    const size_t ndim = ctrl_pairs_.size();
    derivs.assign(ndim, 0.0);

    if (coeffs_.empty()) return 0.0;

    // Special-case single-dimension for efficiency and clarity
    if (ndim == 1) {
        double v = ctrl_v[0];
        double val = 0.0;
        double deriv = 0.0;
        double vpow = 1.0;
        for (size_t i = 0; i < coeffs_.size(); ++i) {
            val += coeffs_[i] * vpow;
            if (i > 0) {
                deriv += static_cast<double>(i) * coeffs_[i] * std::pow(v, static_cast<double>(i) - 1.0);
            }
            vpow *= v;
        }
        // For i == 0, derivative contribution is 0 (constant term)
        // Recompute cleanly
        deriv = 0.0;
        for (size_t i = 1; i < coeffs_.size(); ++i) {
            deriv += static_cast<double>(i) * coeffs_[i] * std::pow(v, static_cast<double>(i) - 1.0);
        }
        derivs[0] = deriv;
        return val;
    }

    // Multi-dimensional POLY: enumerate all monomials in SPICE order.
    // SPICE uses "graded reverse lexicographic" order (by total degree,
    // then descending power of V1, then V2, etc.).
    // Generate terms up to the degree needed to cover all coefficients.
    double val = 0.0;
    size_t coeff_idx = 0;

    // Enumerate by total degree d = 0, 1, 2, ...
    // For each total degree d, enumerate exponent vectors [e1, e2, ..., en]
    // with sum = d, in the SPICE ordering (e1 descends first).
    for (int d = 0; coeff_idx < coeffs_.size(); ++d) {
        // Generate all exponent vectors with sum == d for ndim variables.
        // We use a recursive generation via a stack-based DFS.
        // For ndim=2: d=0 → [0,0]; d=1 → [1,0],[0,1]; d=2 → [2,0],[1,1],[0,2]; ...
        std::vector<int> exp(ndim, 0);
        std::vector<std::vector<int>> exps;

        // Generate combinations using a simple recursive enumeration
        // converted to iterative for clarity.
        // The SPICE ordering for ndim=2 degree d is: e1 from d down to 0, e2 = d - e1
        // For ndim > 2 it extends similarly.
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

            // Evaluate monomial V1^e1 * V2^e2 * ...
            double mono = c;
            for (size_t k = 0; k < ndim; ++k) {
                mono *= std::pow(ctrl_v[k], static_cast<double>(ev[k]));
            }
            val += mono;

            // Partial derivatives: d(mono)/d(Vk) = c * ek * Vk^(ek-1) * prod(Vj^ej, j!=k)
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

void NonlinearVCVS::stamp_pattern(SparsityBuilder& builder) const {
    if (branch_idx_ < 0)
        throw std::logic_error("NonlinearVCVS::stamp_pattern called before set_branch_index");

    // KCL rows: output nodes couple to branch current
    stamp_if_not_ground(builder, np_, branch_idx_);
    stamp_if_not_ground(builder, nn_, branch_idx_);

    // Branch equation row: branch couples to output nodes and all control nodes
    stamp_if_not_ground(builder, branch_idx_, np_);
    stamp_if_not_ground(builder, branch_idx_, nn_);
    for (const auto& cp : ctrl_pairs_) {
        stamp_if_not_ground(builder, branch_idx_, cp.pos);
        stamp_if_not_ground(builder, branch_idx_, cp.neg);
    }
}

void NonlinearVCVS::assign_offsets(const SparsityPattern& pattern) {
    off_np_branch_ = offset_if_not_ground(pattern, np_,         branch_idx_);
    off_nn_branch_ = offset_if_not_ground(pattern, nn_,         branch_idx_);
    off_branch_np_ = offset_if_not_ground(pattern, branch_idx_, np_);
    off_branch_nn_ = offset_if_not_ground(pattern, branch_idx_, nn_);

    off_branch_cp_.resize(ctrl_pairs_.size());
    off_branch_cn_.resize(ctrl_pairs_.size());
    for (size_t k = 0; k < ctrl_pairs_.size(); ++k) {
        off_branch_cp_[k] = offset_if_not_ground(pattern, branch_idx_, ctrl_pairs_[k].pos);
        off_branch_cn_[k] = offset_if_not_ground(pattern, branch_idx_, ctrl_pairs_[k].neg);
    }
}

void NonlinearVCVS::evaluate(const std::vector<double>& voltages,
                              NumericMatrix& mat, std::vector<double>& rhs) {
    // Compute control voltages Vk = V(cpk) - V(cnk)
    std::vector<double> ctrl_v(ctrl_pairs_.size());
    for (size_t k = 0; k < ctrl_pairs_.size(); ++k) {
        double vp = (ctrl_pairs_[k].pos >= 0) ? voltages[ctrl_pairs_[k].pos] : 0.0;
        double vn = (ctrl_pairs_[k].neg >= 0) ? voltages[ctrl_pairs_[k].neg] : 0.0;
        ctrl_v[k] = vp - vn;
    }

    std::vector<double> derivs;
    double f_val = eval_poly(ctrl_v, derivs);

    // KCL at output nodes: +I_branch into np, -I_branch out of nn
    add_if_valid(mat, off_np_branch_,  1.0);
    add_if_valid(mat, off_nn_branch_, -1.0);

    // Branch equation (Jacobian part):
    //   V(np) - V(nn) - f(Vc) = 0
    //   Newton linearization: mat[branch, np] += 1, mat[branch, nn] -= 1,
    //                         mat[branch, cpk] -= df/dVk, mat[branch, cnk] += df/dVk
    //
    //   The linearized branch equation is:
    //   V(np) - V(nn) - sum_k(df/dVk * V(cpk)) + sum_k(df/dVk * V(cnk)) = rhs[branch]
    //   where rhs[branch] = f(Vc_k) - sum_k(df/dVk * Vk_k)   (Newton companion)
    add_if_valid(mat, off_branch_np_,  1.0);
    add_if_valid(mat, off_branch_nn_, -1.0);
    for (size_t k = 0; k < ctrl_pairs_.size(); ++k) {
        add_if_valid(mat, off_branch_cp_[k], -derivs[k]);
        add_if_valid(mat, off_branch_cn_[k],  derivs[k]);
    }

    // RHS: Newton companion for branch equation
    // rhs[branch] = f(Vc_k) - sum_k(df/dVk * Vk_k)
    double rhs_val = f_val;
    for (size_t k = 0; k < ctrl_pairs_.size(); ++k) {
        rhs_val -= derivs[k] * ctrl_v[k];
    }
    add_rhs_if_valid(rhs, branch_idx_, rhs_val);
}

void NonlinearVCVS::ac_stamp(const std::vector<double>& voltages,
                              NumericMatrix& G, NumericMatrix& /*C*/) {
    // For AC analysis, linearize about the DC operating point.
    std::vector<double> ctrl_v(ctrl_pairs_.size());
    for (size_t k = 0; k < ctrl_pairs_.size(); ++k) {
        double vp = (ctrl_pairs_[k].pos >= 0) ? voltages[ctrl_pairs_[k].pos] : 0.0;
        double vn = (ctrl_pairs_[k].neg >= 0) ? voltages[ctrl_pairs_[k].neg] : 0.0;
        ctrl_v[k] = vp - vn;
    }

    std::vector<double> derivs;
    eval_poly(ctrl_v, derivs);

    // AC small-signal stamp (same as linear VCVS with linearized gain)
    add_if_valid(G, off_np_branch_,  1.0);
    add_if_valid(G, off_nn_branch_, -1.0);
    add_if_valid(G, off_branch_np_,  1.0);
    add_if_valid(G, off_branch_nn_, -1.0);
    for (size_t k = 0; k < ctrl_pairs_.size(); ++k) {
        add_if_valid(G, off_branch_cp_[k], -derivs[k]);
        add_if_valid(G, off_branch_cn_[k],  derivs[k]);
    }
}

// ===========================================================================
// TableVCVS — Piecewise-linear table form
// ===========================================================================

TableVCVS::TableVCVS(std::string name,
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
    // Ensure the table is sorted by x
    std::sort(table_.begin(), table_.end(),
              [](const TablePoint& a, const TablePoint& b) { return a.x < b.x; });
}

void TableVCVS::set_branch_index(int32_t idx) {
    branch_idx_ = idx;
}

std::vector<std::string> TableVCVS::output_currents() const {
    return { "I(" + name_ + ")" };
}

double TableVCVS::interp(double x, double& slope) const {
    if (table_.empty()) {
        slope = 0.0;
        return 0.0;
    }
    if (table_.size() == 1) {
        slope = 0.0;
        return table_[0].y;
    }

    // Clamp below — output is constant, so slope = 0
    if (x <= table_.front().x) {
        slope = 0.0;
        return table_.front().y;
    }
    // Clamp above — output is constant, so slope = 0
    if (x >= table_.back().x) {
        slope = 0.0;
        return table_.back().y;
    }

    // Binary search for the segment
    size_t lo = 0, hi = table_.size() - 1;
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if (table_[mid].x <= x) lo = mid;
        else hi = mid;
    }

    double dx = table_[hi].x - table_[lo].x;
    if (dx == 0.0) {
        slope = 0.0;
        return table_[lo].y;
    }
    slope = (table_[hi].y - table_[lo].y) / dx;
    double t = (x - table_[lo].x) / dx;
    return table_[lo].y + t * (table_[hi].y - table_[lo].y);
}

void TableVCVS::stamp_pattern(SparsityBuilder& builder) const {
    if (branch_idx_ < 0)
        throw std::logic_error("TableVCVS::stamp_pattern called before set_branch_index");

    stamp_if_not_ground(builder, np_, branch_idx_);
    stamp_if_not_ground(builder, nn_, branch_idx_);

    stamp_if_not_ground(builder, branch_idx_, np_);
    stamp_if_not_ground(builder, branch_idx_, nn_);
    stamp_if_not_ground(builder, branch_idx_, ncp_);
    stamp_if_not_ground(builder, branch_idx_, ncn_);
}

void TableVCVS::assign_offsets(const SparsityPattern& pattern) {
    off_np_branch_  = offset_if_not_ground(pattern, np_,         branch_idx_);
    off_nn_branch_  = offset_if_not_ground(pattern, nn_,         branch_idx_);
    off_branch_np_  = offset_if_not_ground(pattern, branch_idx_, np_);
    off_branch_nn_  = offset_if_not_ground(pattern, branch_idx_, nn_);
    off_branch_ncp_ = offset_if_not_ground(pattern, branch_idx_, ncp_);
    off_branch_ncn_ = offset_if_not_ground(pattern, branch_idx_, ncn_);
}

void TableVCVS::evaluate(const std::vector<double>& voltages,
                          NumericMatrix& mat, std::vector<double>& rhs) {
    double vp = (ncp_ >= 0) ? voltages[ncp_] : 0.0;
    double vn = (ncn_ >= 0) ? voltages[ncn_] : 0.0;
    double vc = vp - vn;

    double slope = 0.0;
    double f_val = interp(vc, slope);

    // KCL at output nodes
    add_if_valid(mat, off_np_branch_,  1.0);
    add_if_valid(mat, off_nn_branch_, -1.0);

    // Branch equation Jacobian:
    //   V(np) - V(nn) - slope*(V(ncp) - V(ncn)) = rhs[branch]
    //   rhs[branch] = f(Vc_k) - slope * Vc_k   (Newton companion)
    add_if_valid(mat, off_branch_np_,   1.0);
    add_if_valid(mat, off_branch_nn_,  -1.0);
    add_if_valid(mat, off_branch_ncp_, -slope);
    add_if_valid(mat, off_branch_ncn_,  slope);

    // RHS: Newton companion
    double rhs_val = f_val - slope * vc;
    add_rhs_if_valid(rhs, branch_idx_, rhs_val);
}

void TableVCVS::ac_stamp(const std::vector<double>& voltages,
                          NumericMatrix& G, NumericMatrix& /*C*/) {
    double vp = (ncp_ >= 0) ? voltages[ncp_] : 0.0;
    double vn = (ncn_ >= 0) ? voltages[ncn_] : 0.0;
    double vc = vp - vn;

    double slope = 0.0;
    interp(vc, slope);

    add_if_valid(G, off_np_branch_,  1.0);
    add_if_valid(G, off_nn_branch_, -1.0);
    add_if_valid(G, off_branch_np_,   1.0);
    add_if_valid(G, off_branch_nn_,  -1.0);
    add_if_valid(G, off_branch_ncp_, -slope);
    add_if_valid(G, off_branch_ncn_,  slope);
}

} // namespace neospice
