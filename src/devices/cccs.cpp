#include "devices/cccs.hpp"
#include <stdexcept>

namespace neospice {

CCCS::CCCS(std::string name,
           int32_t node_pos, int32_t node_neg,
           double gain,
           const VSource* vsense)
    : Device(std::move(name))
    , np_(node_pos)
    , nn_(node_neg)
    , gain_(gain)
    , vsense_(vsense)
{
    if (!vsense_)
        throw std::invalid_argument("CCCS: vsense pointer must not be null");
}

void CCCS::stamp_pattern(SparsityBuilder& builder) const {
    int32_t sense_branch = vsense_->branch_index();
    if (sense_branch < 0)
        throw std::logic_error("CCCS::stamp_pattern: sensing VSource branch index not yet assigned");

    // sense_branch is always >= 0 (it's a branch variable index, never ground)
    // Only np_ and nn_ need the ground check
    if (np_ >= 0) builder.add(np_, sense_branch);
    if (nn_ >= 0) builder.add(nn_, sense_branch);
}

void CCCS::assign_offsets(const SparsityPattern& pattern) {
    int32_t sense_branch = vsense_->branch_index();

    off_np_sense_ = (np_ >= 0) ? pattern.offset(np_, sense_branch) : -1;
    off_nn_sense_ = (nn_ >= 0) ? pattern.offset(nn_, sense_branch) : -1;
}

void CCCS::evaluate(const std::vector<double>& /*voltages*/,
                    NumericMatrix& mat, std::vector<double>& /*rhs*/) {
    // SPICE convention: I = gain * I(Vsense) leaves N+ (np).
    // Current leaving np = +gain*I_sense → mat[np, sense_branch] += +gain
    // Current leaving nn = -gain*I_sense → mat[nn, sense_branch] += -gain
    const double gain = gain_ * m_;
    add_if_valid(mat, off_np_sense_,  gain);
    add_if_valid(mat, off_nn_sense_, -gain);
}

void CCCS::ac_stamp(const std::vector<double>& /*voltages*/,
                    NumericMatrix& G, NumericMatrix& /*C*/) {
    const double gain = gain_ * m_;
    add_if_valid(G, off_np_sense_,  gain);
    add_if_valid(G, off_nn_sense_, -gain);
}

} // namespace neospice
