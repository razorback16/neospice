#include "devices/ccvs_nonlinear.hpp"
#include "core/circuit.hpp"   // tls_integrator_ctx
#include "devices/spice2poly.hpp"
#include "devices/xspice_input_limiter.hpp"
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
    return eval_spice2poly(ctrl_i, coeffs_, derivs);
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
    inputs_limited_ = xspice_limit_analog_inputs(
        ctrl_i, last_inputs_, input_state0_, input_state1_);

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
    add_rhs_if_valid(rhs, branch_idx_, f_val * dsf);
    for (size_t k = 0; k < vsenses_.size(); ++k) {
        const double partial = derivs[k] * dsf;
        add_if_valid(mat, off_branch_sense_[k], -partial);
        add_rhs_if_valid(rhs, branch_idx_, -partial * ctrl_i[k]);
    }
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
