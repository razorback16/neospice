#include "devices/cccs_nonlinear.hpp"
#include <span>
#include "core/circuit.hpp"   // tls_integrator_ctx
#include "devices/spice2poly.hpp"
#include "devices/xspice_input_limiter.hpp"
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
    return eval_spice2poly(ctrl_i, coeffs_, derivs);
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
                              NumericMatrix& mat, std::span<double> rhs) {
    // Compute sensing currents
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

    const double output = f_val * dsf;
    add_rhs_if_valid(rhs, np_, -output);
    add_rhs_if_valid(rhs, nn_,  output);

    // SPICE convention: I = f(Ic) leaves N+ (np).
    // Jacobian: mat[np, sense_branch_k] += df/dIk
    //           mat[nn, sense_branch_k] -= df/dIk
    for (size_t k = 0; k < vsenses_.size(); ++k) {
        const double partial = derivs[k] * dsf;
        add_if_valid(mat, off_np_sense_[k],  partial);
        add_if_valid(mat, off_nn_sense_[k], -partial);
        const double temp = partial * ctrl_i[k];
        add_rhs_if_valid(rhs, np_,  temp);
        add_rhs_if_valid(rhs, nn_, -temp);
    }
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
