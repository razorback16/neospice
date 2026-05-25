#include "devices/vccs_nonlinear.hpp"
#include "devices/vsource.hpp"
#include <cmath>
#include <algorithm>
#include <functional>
#include <cassert>

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

    // SPICE convention: I = f(Vc) leaves N+ (np).
    // Jacobian: current leaving np = +df/dVk * V(cpk) - df/dVk * V(cnk)
    for (size_t k = 0; k < ctrl_pairs_.size(); ++k) {
        add_if_valid(mat, off_np_cp_[k],  derivs[k]);
        add_if_valid(mat, off_np_cn_[k], -derivs[k]);
        add_if_valid(mat, off_nn_cp_[k], -derivs[k]);
        add_if_valid(mat, off_nn_cn_[k],  derivs[k]);
    }

    // Newton companion: rhs[np] = -(f(Vc_k) - sum(df/dVk * Vc_k))
    double companion = f_val;
    for (size_t k = 0; k < ctrl_pairs_.size(); ++k) {
        companion -= derivs[k] * ctrl_v[k];
    }
    add_rhs_if_valid(rhs, np_, -companion);
    add_rhs_if_valid(rhs, nn_,  companion);
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
        add_if_valid(G, off_np_cp_[k],  derivs[k]);
        add_if_valid(G, off_np_cn_[k], -derivs[k]);
        add_if_valid(G, off_nn_cp_[k], -derivs[k]);
        add_if_valid(G, off_nn_cn_[k],  derivs[k]);
    }
}

// ===========================================================================
// TableVCCS — Piecewise-linear table form
// ===========================================================================

// Simple constructor: V(ctrl_pos) - V(ctrl_neg) control
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

// Expression constructor: arbitrary expression control
TableVCCS::TableVCCS(std::string name,
                     int32_t node_pos, int32_t node_neg,
                     asrc::CompiledExpression expr,
                     std::vector<int32_t> var_indices,
                     std::vector<int32_t> var_indices2,
                     std::vector<const VSource*> vsource_ptrs,
                     std::vector<TablePoint> table_points)
    : Device(std::move(name))
    , np_(node_pos)
    , nn_(node_neg)
    , has_expr_(true)
    , expr_(std::move(expr))
    , var_indices_(std::move(var_indices))
    , var_indices2_(std::move(var_indices2))
    , vsource_ptrs_(std::move(vsource_ptrs))
    , table_(std::move(table_points))
{
    int nv = expr_.num_vars();
    var_values_.resize(nv, 0.0);
    derivs_.resize(nv, 0.0);
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

int32_t TableVCCS::var_circuit_index(int i) const {
    const auto& ref = expr_.var_refs()[i];
    if (ref.kind == asrc::VarKind::BRANCH_CURRENT) {
        assert(vsource_ptrs_[i] != nullptr);
        return vsource_ptrs_[i]->branch_index();
    }
    return var_indices_[i];
}

void TableVCCS::fill_var_values(const std::vector<double>& voltages) {
    const auto& refs = expr_.var_refs();
    for (int i = 0; i < static_cast<int>(refs.size()); ++i) {
        switch (refs[i].kind) {
        case asrc::VarKind::NODE_VOLTAGE: {
            int32_t ni = var_indices_[i];
            var_values_[i] = (ni >= 0) ? voltages[ni] : 0.0;
            break;
        }
        case asrc::VarKind::DIFF_VOLTAGE: {
            int32_t n1 = var_indices_[i];
            int32_t n2 = var_indices2_[i];
            double v1 = (n1 >= 0) ? voltages[n1] : 0.0;
            double v2 = (n2 >= 0) ? voltages[n2] : 0.0;
            var_values_[i] = v1 - v2;
            break;
        }
        case asrc::VarKind::BRANCH_CURRENT: {
            int32_t br = var_circuit_index(i);
            var_values_[i] = (br >= 0) ? voltages[br] : 0.0;
            break;
        }
        }
    }
}

std::vector<int32_t> TableVCCS::external_nodes() const {
    if (!has_expr_) return {np_, nn_, ncp_, ncn_};
    std::vector<int32_t> nodes = {np_, nn_};
    for (auto n : var_indices_) nodes.push_back(n);
    for (auto n : var_indices2_) if (n >= 0) nodes.push_back(n);
    return nodes;
}

void TableVCCS::stamp_pattern(SparsityBuilder& builder) const {
    if (!has_expr_) {
        stamp_if_not_ground(builder, np_, ncp_);
        stamp_if_not_ground(builder, np_, ncn_);
        stamp_if_not_ground(builder, nn_, ncp_);
        stamp_if_not_ground(builder, nn_, ncn_);
        return;
    }
    const auto& refs = expr_.var_refs();
    for (int i = 0; i < static_cast<int>(refs.size()); ++i) {
        switch (refs[i].kind) {
        case asrc::VarKind::NODE_VOLTAGE:
            stamp_if_not_ground(builder, np_, var_indices_[i]);
            stamp_if_not_ground(builder, nn_, var_indices_[i]);
            break;
        case asrc::VarKind::DIFF_VOLTAGE:
            stamp_if_not_ground(builder, np_, var_indices_[i]);
            stamp_if_not_ground(builder, nn_, var_indices_[i]);
            stamp_if_not_ground(builder, np_, var_indices2_[i]);
            stamp_if_not_ground(builder, nn_, var_indices2_[i]);
            break;
        case asrc::VarKind::BRANCH_CURRENT: {
            int32_t br = var_circuit_index(i);
            stamp_if_not_ground(builder, np_, br);
            stamp_if_not_ground(builder, nn_, br);
            break;
        }
        }
    }
}

void TableVCCS::assign_offsets(const SparsityPattern& pattern) {
    if (!has_expr_) {
        off_np_ncp_ = offset_if_not_ground(pattern, np_, ncp_);
        off_np_ncn_ = offset_if_not_ground(pattern, np_, ncn_);
        off_nn_ncp_ = offset_if_not_ground(pattern, nn_, ncp_);
        off_nn_ncn_ = offset_if_not_ground(pattern, nn_, ncn_);
        return;
    }
    const auto& refs = expr_.var_refs();
    var_stamps_.resize(refs.size());
    for (int i = 0; i < static_cast<int>(refs.size()); ++i) {
        switch (refs[i].kind) {
        case asrc::VarKind::NODE_VOLTAGE:
            var_stamps_[i].off_np = offset_if_not_ground(pattern, np_, var_indices_[i]);
            var_stamps_[i].off_nn = offset_if_not_ground(pattern, nn_, var_indices_[i]);
            break;
        case asrc::VarKind::DIFF_VOLTAGE:
            var_stamps_[i].off_np  = offset_if_not_ground(pattern, np_, var_indices_[i]);
            var_stamps_[i].off_nn  = offset_if_not_ground(pattern, nn_, var_indices_[i]);
            var_stamps_[i].off_np2 = offset_if_not_ground(pattern, np_, var_indices2_[i]);
            var_stamps_[i].off_nn2 = offset_if_not_ground(pattern, nn_, var_indices2_[i]);
            break;
        case asrc::VarKind::BRANCH_CURRENT: {
            int32_t br = var_circuit_index(i);
            var_stamps_[i].off_np = offset_if_not_ground(pattern, np_, br);
            var_stamps_[i].off_nn = offset_if_not_ground(pattern, nn_, br);
            break;
        }
        }
    }
}

void TableVCCS::evaluate(const std::vector<double>& voltages,
                          NumericMatrix& mat, std::vector<double>& rhs) {
    if (!has_expr_) {
        // Simple mode: V(ctrl_pos) - V(ctrl_neg)
        double vp = (ncp_ >= 0) ? voltages[ncp_] : 0.0;
        double vn = (ncn_ >= 0) ? voltages[ncn_] : 0.0;
        double vc = vp - vn;

        double slope = 0.0;
        double f_val = interp(vc, slope);

        add_if_valid(mat, off_np_ncp_,  slope);
        add_if_valid(mat, off_np_ncn_, -slope);
        add_if_valid(mat, off_nn_ncp_, -slope);
        add_if_valid(mat, off_nn_ncn_,  slope);

        double companion = f_val - slope * vc;
        add_rhs_if_valid(rhs, np_, -companion);
        add_rhs_if_valid(rhs, nn_,  companion);
        return;
    }

    // Expression mode: evaluate expression for table control value
    fill_var_values(voltages);
    double ctrl_val = expr_.evaluate(var_values_, derivs_);

    double table_slope = 0.0;
    double f_val = interp(ctrl_val, table_slope);

    // Chain rule: dI/dv_i = table_slope * d(expr)/d(v_i)
    const auto& refs = expr_.var_refs();
    for (int i = 0; i < static_cast<int>(refs.size()); ++i) {
        double jac = table_slope * derivs_[i];
        switch (refs[i].kind) {
        case asrc::VarKind::NODE_VOLTAGE:
            add_if_valid(mat, var_stamps_[i].off_np,  jac);
            add_if_valid(mat, var_stamps_[i].off_nn, -jac);
            break;
        case asrc::VarKind::DIFF_VOLTAGE:
            add_if_valid(mat, var_stamps_[i].off_np,   jac);
            add_if_valid(mat, var_stamps_[i].off_nn,  -jac);
            add_if_valid(mat, var_stamps_[i].off_np2, -jac);
            add_if_valid(mat, var_stamps_[i].off_nn2,  jac);
            break;
        case asrc::VarKind::BRANCH_CURRENT:
            add_if_valid(mat, var_stamps_[i].off_np,  jac);
            add_if_valid(mat, var_stamps_[i].off_nn, -jac);
            break;
        }
    }

    // Newton companion RHS
    double lin_sum = 0.0;
    for (int i = 0; i < static_cast<int>(refs.size()); ++i) {
        double jac = table_slope * derivs_[i];
        switch (refs[i].kind) {
        case asrc::VarKind::NODE_VOLTAGE: {
            int32_t ni = var_indices_[i];
            double vi = (ni >= 0) ? voltages[ni] : 0.0;
            lin_sum += jac * vi;
            break;
        }
        case asrc::VarKind::DIFF_VOLTAGE: {
            int32_t n1 = var_indices_[i], n2 = var_indices2_[i];
            double v1 = (n1 >= 0) ? voltages[n1] : 0.0;
            double v2 = (n2 >= 0) ? voltages[n2] : 0.0;
            lin_sum += jac * v1;
            lin_sum -= jac * v2;
            break;
        }
        case asrc::VarKind::BRANCH_CURRENT: {
            int32_t br = var_circuit_index(i);
            double ib = (br >= 0) ? voltages[br] : 0.0;
            lin_sum += jac * ib;
            break;
        }
        }
    }
    double companion = f_val - lin_sum;
    add_rhs_if_valid(rhs, np_, -companion);
    add_rhs_if_valid(rhs, nn_,  companion);
}

void TableVCCS::ac_stamp(const std::vector<double>& voltages,
                          NumericMatrix& G, NumericMatrix& /*C*/) {
    if (!has_expr_) {
        double vp = (ncp_ >= 0) ? voltages[ncp_] : 0.0;
        double vn = (ncn_ >= 0) ? voltages[ncn_] : 0.0;
        double vc = vp - vn;

        double slope = 0.0;
        interp(vc, slope);

        add_if_valid(G, off_np_ncp_,  slope);
        add_if_valid(G, off_np_ncn_, -slope);
        add_if_valid(G, off_nn_ncp_, -slope);
        add_if_valid(G, off_nn_ncn_,  slope);
        return;
    }

    fill_var_values(voltages);
    double ctrl_val = expr_.evaluate(var_values_, derivs_);

    double table_slope = 0.0;
    interp(ctrl_val, table_slope);

    const auto& refs = expr_.var_refs();
    for (int i = 0; i < static_cast<int>(refs.size()); ++i) {
        double jac = table_slope * derivs_[i];
        switch (refs[i].kind) {
        case asrc::VarKind::NODE_VOLTAGE:
            add_if_valid(G, var_stamps_[i].off_np,  jac);
            add_if_valid(G, var_stamps_[i].off_nn, -jac);
            break;
        case asrc::VarKind::DIFF_VOLTAGE:
            add_if_valid(G, var_stamps_[i].off_np,   jac);
            add_if_valid(G, var_stamps_[i].off_nn,  -jac);
            add_if_valid(G, var_stamps_[i].off_np2, -jac);
            add_if_valid(G, var_stamps_[i].off_nn2,  jac);
            break;
        case asrc::VarKind::BRANCH_CURRENT:
            add_if_valid(G, var_stamps_[i].off_np,  jac);
            add_if_valid(G, var_stamps_[i].off_nn, -jac);
            break;
        }
    }
}

} // namespace neospice
