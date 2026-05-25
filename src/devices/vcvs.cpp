#include "devices/vcvs.hpp"
#include "core/circuit.hpp"   // tls_integrator_ctx
#include <stdexcept>

namespace neospice {

VCVS::VCVS(std::string name,
           int32_t node_pos, int32_t node_neg,
           int32_t node_ctrl_pos, int32_t node_ctrl_neg,
           double gain)
    : Device(std::move(name))
    , np_(node_pos)
    , nn_(node_neg)
    , ncp_(node_ctrl_pos)
    , ncn_(node_ctrl_neg)
    , gain_(gain)
{}

void VCVS::set_branch_index(int32_t idx) {
    branch_idx_ = idx;
}

std::vector<std::string> VCVS::output_currents() const {
    return { "I(" + name_ + ")" };
}

void VCVS::stamp_pattern(SparsityBuilder& builder) const {
    if (branch_idx_ < 0)
        throw std::logic_error("VCVS::stamp_pattern called before set_branch_index");

    // KCL rows: output nodes couple to branch current
    stamp_if_not_ground(builder, np_, branch_idx_);
    stamp_if_not_ground(builder, nn_, branch_idx_);

    // Branch equation row: branch couples to output nodes and control nodes
    stamp_if_not_ground(builder, branch_idx_, np_);
    stamp_if_not_ground(builder, branch_idx_, nn_);
    stamp_if_not_ground(builder, branch_idx_, ncp_);
    stamp_if_not_ground(builder, branch_idx_, ncn_);
}

void VCVS::assign_offsets(const SparsityPattern& pattern) {
    off_np_branch_  = offset_if_not_ground(pattern, np_,         branch_idx_);
    off_nn_branch_  = offset_if_not_ground(pattern, nn_,         branch_idx_);
    off_branch_np_  = offset_if_not_ground(pattern, branch_idx_, np_);
    off_branch_nn_  = offset_if_not_ground(pattern, branch_idx_, nn_);
    off_branch_ncp_ = offset_if_not_ground(pattern, branch_idx_, ncp_);
    off_branch_ncn_ = offset_if_not_ground(pattern, branch_idx_, ncn_);
}

void VCVS::evaluate(const std::vector<double>& /*voltages*/,
                    NumericMatrix& mat, std::vector<double>& /*rhs*/) {
    // Scale gain by dep_src_fact for gain stepping convergence aid
    double gain = gain_;
    if (tls_integrator_ctx && tls_integrator_ctx->options)
        gain *= tls_integrator_ctx->options->dep_src_fact;

    // KCL at output nodes: +I_branch into np, -I_branch out of nn
    add_if_valid(mat, off_np_branch_,  1.0);
    add_if_valid(mat, off_nn_branch_, -1.0);

    // Branch equation: V(np) - V(nn) - gain*(V(ncp) - V(ncn)) = 0
    add_if_valid(mat, off_branch_np_,   1.0);
    add_if_valid(mat, off_branch_nn_,  -1.0);
    add_if_valid(mat, off_branch_ncp_, -gain);
    add_if_valid(mat, off_branch_ncn_,  gain);
    // RHS = 0 (no independent voltage source term)
}

void VCVS::ac_stamp(const std::vector<double>& /*voltages*/,
                    NumericMatrix& G, NumericMatrix& /*C*/) {
    // Linear device — AC stamp is identical to DC evaluate, into G matrix only
    add_if_valid(G, off_np_branch_,  1.0);
    add_if_valid(G, off_nn_branch_, -1.0);
    add_if_valid(G, off_branch_np_,   1.0);
    add_if_valid(G, off_branch_nn_,  -1.0);
    add_if_valid(G, off_branch_ncp_, -gain_);
    add_if_valid(G, off_branch_ncn_,  gain_);
}

} // namespace neospice
