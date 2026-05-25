#include "devices/ccvs.hpp"
#include "core/circuit.hpp"   // tls_integrator_ctx
#include <stdexcept>

namespace neospice {

CCVS::CCVS(std::string name,
           int32_t node_pos, int32_t node_neg,
           double rm,
           const VSource* vsense)
    : Device(std::move(name))
    , np_(node_pos)
    , nn_(node_neg)
    , rm_(rm)
    , vsense_(vsense)
{
    if (!vsense_)
        throw std::invalid_argument("CCVS: vsense pointer must not be null");
}

void CCVS::set_branch_index(int32_t idx) {
    branch_idx_ = idx;
}

std::vector<std::string> CCVS::output_currents() const {
    return { "I(" + name_ + ")" };
}

void CCVS::stamp_pattern(SparsityBuilder& builder) const {
    if (branch_idx_ < 0)
        throw std::logic_error("CCVS::stamp_pattern called before set_branch_index");

    int32_t sense_branch = vsense_->branch_index();
    if (sense_branch < 0)
        throw std::logic_error("CCVS::stamp_pattern: sensing VSource branch index not yet assigned");

    // KCL rows: output nodes couple to this branch current
    stamp_if_not_ground(builder, np_, branch_idx_);
    stamp_if_not_ground(builder, nn_, branch_idx_);

    // Branch equation row: branch couples to output nodes and sense branch
    stamp_if_not_ground(builder, branch_idx_, np_);
    stamp_if_not_ground(builder, branch_idx_, nn_);
    // sense_branch is always >= 0 (it's a branch variable index, never ground)
    builder.add(branch_idx_, sense_branch);
}

void CCVS::assign_offsets(const SparsityPattern& pattern) {
    int32_t sense_branch = vsense_->branch_index();

    off_np_branch_    = offset_if_not_ground(pattern, np_,         branch_idx_);
    off_nn_branch_    = offset_if_not_ground(pattern, nn_,         branch_idx_);
    off_branch_np_    = offset_if_not_ground(pattern, branch_idx_, np_);
    off_branch_nn_    = offset_if_not_ground(pattern, branch_idx_, nn_);
    off_branch_sense_ = pattern.offset(branch_idx_, sense_branch);
}

void CCVS::evaluate(const std::vector<double>& /*voltages*/,
                    NumericMatrix& mat, std::vector<double>& /*rhs*/) {
    // KCL at output nodes: +I_branch into np, -I_branch out of nn
    add_if_valid(mat, off_np_branch_,  1.0);
    add_if_valid(mat, off_nn_branch_, -1.0);

    // Branch equation: V(np) - V(nn) - R_m * I_sense = 0
    double rm = rm_;
    if (tls_integrator_ctx && tls_integrator_ctx->options)
        rm *= tls_integrator_ctx->options->dep_src_fact;
    add_if_valid(mat, off_branch_np_,      1.0);
    add_if_valid(mat, off_branch_nn_,     -1.0);
    add_if_valid(mat, off_branch_sense_, -rm);
    // RHS = 0 (no independent source term)
}

void CCVS::ac_stamp(const std::vector<double>& /*voltages*/,
                    NumericMatrix& G, NumericMatrix& /*C*/) {
    // Linear device — AC stamp is identical to DC evaluate, into G matrix only
    add_if_valid(G, off_np_branch_,  1.0);
    add_if_valid(G, off_nn_branch_, -1.0);
    add_if_valid(G, off_branch_np_,      1.0);
    add_if_valid(G, off_branch_nn_,     -1.0);
    add_if_valid(G, off_branch_sense_, -rm_);
}

} // namespace neospice
