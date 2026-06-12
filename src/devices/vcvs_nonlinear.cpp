#include "devices/vcvs_nonlinear.hpp"
#include "core/circuit.hpp"   // tls_integrator_ctx
#include "devices/vsource.hpp"
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <functional>
#include <cassert>

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

    // Scale by dep_src_fact for gain stepping convergence aid
    double dsf = 1.0;
    if (tls_integrator_ctx && tls_integrator_ctx->options)
        dsf = tls_integrator_ctx->options->dep_src_fact;
    f_val *= dsf;
    for (auto& d : derivs) d *= dsf;

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

// Expression constructor: arbitrary scalar expression control.
TableVCVS::TableVCVS(std::string name,
                     int32_t node_pos, int32_t node_neg,
                     asrc::CompiledExpression expr,
                     std::vector<int32_t> var_indices,
                     std::vector<int32_t> var_indices2,
                     std::vector<const VSource*> vsource_ptrs,
                     std::vector<TablePoint> table_points)
    : Device(std::move(name))
    , np_(node_pos)
    , nn_(node_neg)
    , table_(std::move(table_points))
    , has_expr_(true)
    , expr_(std::move(expr))
    , var_indices_(std::move(var_indices))
    , var_indices2_(std::move(var_indices2))
    , vsource_ptrs_(std::move(vsource_ptrs))
{
    int nv = expr_.num_vars();
    var_values_.resize(nv, 0.0);
    derivs_.resize(nv, 0.0);
    std::sort(table_.begin(), table_.end(),
              [](const TablePoint& a, const TablePoint& b) { return a.x < b.x; });

    // Locate special simulator-variable indices (TIME/TEMPER/HERTZ).  These are
    // surfaced by the ASRC compiler as NODE_VOLTAGE refs with sentinel names and
    // carry the -2 node index — they are NOT circuit nodes and must be skipped in
    // stamping / node enumeration and injected with the live value at evaluate.
    const auto& refs = expr_.var_refs();
    for (int i = 0; i < nv; ++i) {
        if (refs[i].kind != asrc::VarKind::NODE_VOLTAGE) continue;
        if (refs[i].name1 == "__time__")   time_var_idx_   = i;
        else if (refs[i].name1 == "__temper__") temper_var_idx_ = i;
        else if (refs[i].name1 == "__hertz__")  hertz_var_idx_  = i;
    }
}

void TableVCVS::set_branch_index(int32_t idx) {
    branch_idx_ = idx;
}

int32_t TableVCVS::var_circuit_index(int i) const {
    const auto& ref = expr_.var_refs()[i];
    if (ref.kind == asrc::VarKind::BRANCH_CURRENT) {
        assert(vsource_ptrs_[i] != nullptr);
        return vsource_ptrs_[i]->branch_index();
    }
    return var_indices_[i];
}

void TableVCVS::fill_var_values(const std::vector<double>& voltages) {
    const auto& refs = expr_.var_refs();
    for (int i = 0; i < static_cast<int>(refs.size()); ++i) {
        // Special simulator variables — injected from the active IntegratorCtx,
        // never read from the solution vector (their node index is the -2
        // sentinel).  Mirrors ASRCDevice::fill_var_values.
        if (i == time_var_idx_) {
            var_values_[i] = tls_integrator_ctx ? tls_integrator_ctx->current_time : 0.0;
            continue;
        }
        if (i == temper_var_idx_) {
            double temp_celsius = 27.0;
            if (tls_integrator_ctx && tls_integrator_ctx->options)
                temp_celsius = tls_integrator_ctx->options->temp - 273.15;
            var_values_[i] = temp_celsius;
            continue;
        }
        if (i == hertz_var_idx_) {
            var_values_[i] = tls_integrator_ctx ? tls_integrator_ctx->ac_freq : 0.0;
            continue;
        }
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

std::vector<int32_t> TableVCVS::external_nodes() const {
    if (!has_expr_) return {np_, nn_, ncp_, ncn_};
    std::vector<int32_t> nodes = {np_, nn_};
    // Skip negative indices: GROUND (-1) and the TIME/TEMPER/HERTZ sentinel (-2)
    // are not circuit nodes and must not register as device connections (a -2
    // here surfaced as a spurious floating '__time__' node).
    for (auto n : var_indices_)  if (n >= 0) nodes.push_back(n);
    for (auto n : var_indices2_) if (n >= 0) nodes.push_back(n);
    return nodes;
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
    // Match ngspice's E/G TABLE smoothing (padding + parabolic corners).
    return pwl_smooth_interp(table_, x, slope);
}

void TableVCVS::stamp_pattern(SparsityBuilder& builder) const {
    if (branch_idx_ < 0)
        throw std::logic_error("TableVCVS::stamp_pattern called before set_branch_index");

    // KCL rows: output nodes couple to branch current
    stamp_if_not_ground(builder, np_, branch_idx_);
    stamp_if_not_ground(builder, nn_, branch_idx_);

    // Branch equation row couples to output nodes
    stamp_if_not_ground(builder, branch_idx_, np_);
    stamp_if_not_ground(builder, branch_idx_, nn_);

    if (!has_expr_) {
        stamp_if_not_ground(builder, branch_idx_, ncp_);
        stamp_if_not_ground(builder, branch_idx_, ncn_);
        return;
    }
    // Expression mode: branch row couples to every referenced variable.
    const auto& refs = expr_.var_refs();
    for (int i = 0; i < static_cast<int>(refs.size()); ++i) {
        switch (refs[i].kind) {
        case asrc::VarKind::NODE_VOLTAGE:
            stamp_if_not_ground(builder, branch_idx_, var_indices_[i]);
            break;
        case asrc::VarKind::DIFF_VOLTAGE:
            stamp_if_not_ground(builder, branch_idx_, var_indices_[i]);
            stamp_if_not_ground(builder, branch_idx_, var_indices2_[i]);
            break;
        case asrc::VarKind::BRANCH_CURRENT:
            stamp_if_not_ground(builder, branch_idx_, var_circuit_index(i));
            break;
        }
    }
}

void TableVCVS::assign_offsets(const SparsityPattern& pattern) {
    off_np_branch_  = offset_if_not_ground(pattern, np_,         branch_idx_);
    off_nn_branch_  = offset_if_not_ground(pattern, nn_,         branch_idx_);
    off_branch_np_  = offset_if_not_ground(pattern, branch_idx_, np_);
    off_branch_nn_  = offset_if_not_ground(pattern, branch_idx_, nn_);

    if (!has_expr_) {
        off_branch_ncp_ = offset_if_not_ground(pattern, branch_idx_, ncp_);
        off_branch_ncn_ = offset_if_not_ground(pattern, branch_idx_, ncn_);
        return;
    }
    const auto& refs = expr_.var_refs();
    var_stamps_.resize(refs.size());
    for (int i = 0; i < static_cast<int>(refs.size()); ++i) {
        switch (refs[i].kind) {
        case asrc::VarKind::NODE_VOLTAGE:
            var_stamps_[i].off_branch = offset_if_not_ground(pattern, branch_idx_, var_indices_[i]);
            break;
        case asrc::VarKind::DIFF_VOLTAGE:
            var_stamps_[i].off_branch  = offset_if_not_ground(pattern, branch_idx_, var_indices_[i]);
            var_stamps_[i].off_branch2 = offset_if_not_ground(pattern, branch_idx_, var_indices2_[i]);
            break;
        case asrc::VarKind::BRANCH_CURRENT:
            var_stamps_[i].off_branch = offset_if_not_ground(pattern, branch_idx_, var_circuit_index(i));
            break;
        }
    }
}

void TableVCVS::evaluate(const std::vector<double>& voltages,
                          NumericMatrix& mat, std::vector<double>& rhs) {
    // Scale by dep_src_fact for gain stepping convergence aid
    double dsf = 1.0;
    if (tls_integrator_ctx && tls_integrator_ctx->options)
        dsf = tls_integrator_ctx->options->dep_src_fact;

    // KCL at output nodes (common to both modes)
    add_if_valid(mat, off_np_branch_,  1.0);
    add_if_valid(mat, off_nn_branch_, -1.0);
    add_if_valid(mat, off_branch_np_,   1.0);
    add_if_valid(mat, off_branch_nn_,  -1.0);

    if (!has_expr_) {
        double vp = (ncp_ >= 0) ? voltages[ncp_] : 0.0;
        double vn = (ncn_ >= 0) ? voltages[ncn_] : 0.0;
        double vc = vp - vn;

        double slope = 0.0;
        double f_val = interp(vc, slope);
        f_val *= dsf;
        slope *= dsf;

        // Branch equation: V(np) - V(nn) - slope*(V(ncp)-V(ncn)) = rhs[branch]
        add_if_valid(mat, off_branch_ncp_, -slope);
        add_if_valid(mat, off_branch_ncn_,  slope);

        double rhs_val = f_val - slope * vc;
        add_rhs_if_valid(rhs, branch_idx_, rhs_val);
        return;
    }

    // Expression mode: evaluate control expression x and its gradient,
    // then chain-rule the table slope onto every referenced variable.
    fill_var_values(voltages);
    double ctrl_val = expr_.evaluate(var_values_, derivs_);

    double table_slope = 0.0;
    double f_val = interp(ctrl_val, table_slope);
    f_val *= dsf;
    table_slope *= dsf;

    // Branch equation: V(np) - V(nn) - f(x) = 0, with
    //   d(f)/d(v_k) = table_slope * d(expr)/d(v_k).
    // mat[branch, v_k] -= d(f)/d(v_k); rhs companion accumulates slope*var.
    const auto& refs = expr_.var_refs();
    double lin_sum = 0.0;
    for (int i = 0; i < static_cast<int>(refs.size()); ++i) {
        double jac = table_slope * derivs_[i];
        switch (refs[i].kind) {
        case asrc::VarKind::NODE_VOLTAGE: {
            add_if_valid(mat, var_stamps_[i].off_branch, -jac);
            int32_t ni = var_indices_[i];
            lin_sum += jac * ((ni >= 0) ? voltages[ni] : 0.0);
            break;
        }
        case asrc::VarKind::DIFF_VOLTAGE: {
            add_if_valid(mat, var_stamps_[i].off_branch,  -jac);
            add_if_valid(mat, var_stamps_[i].off_branch2,  jac);
            int32_t n1 = var_indices_[i], n2 = var_indices2_[i];
            double v1 = (n1 >= 0) ? voltages[n1] : 0.0;
            double v2 = (n2 >= 0) ? voltages[n2] : 0.0;
            lin_sum += jac * (v1 - v2);
            break;
        }
        case asrc::VarKind::BRANCH_CURRENT: {
            add_if_valid(mat, var_stamps_[i].off_branch, -jac);
            int32_t br = var_circuit_index(i);
            lin_sum += jac * ((br >= 0) ? voltages[br] : 0.0);
            break;
        }
        }
    }

    // Newton companion: rhs[branch] = f(x) - sum_k(d(f)/d(v_k) * v_k)
    double rhs_val = f_val - lin_sum;
    add_rhs_if_valid(rhs, branch_idx_, rhs_val);
}

void TableVCVS::ac_stamp(const std::vector<double>& voltages,
                          NumericMatrix& G, NumericMatrix& /*C*/) {
    add_if_valid(G, off_np_branch_,  1.0);
    add_if_valid(G, off_nn_branch_, -1.0);
    add_if_valid(G, off_branch_np_,   1.0);
    add_if_valid(G, off_branch_nn_,  -1.0);

    if (!has_expr_) {
        double vp = (ncp_ >= 0) ? voltages[ncp_] : 0.0;
        double vn = (ncn_ >= 0) ? voltages[ncn_] : 0.0;
        double vc = vp - vn;
        double slope = 0.0;
        interp(vc, slope);
        add_if_valid(G, off_branch_ncp_, -slope);
        add_if_valid(G, off_branch_ncn_,  slope);
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
            add_if_valid(G, var_stamps_[i].off_branch, -jac);
            break;
        case asrc::VarKind::DIFF_VOLTAGE:
            add_if_valid(G, var_stamps_[i].off_branch,  -jac);
            add_if_valid(G, var_stamps_[i].off_branch2,  jac);
            break;
        case asrc::VarKind::BRANCH_CURRENT:
            add_if_valid(G, var_stamps_[i].off_branch, -jac);
            break;
        }
    }
}

} // namespace neospice
