#include "devices/vccs.hpp"

namespace neospice {

VCCS::VCCS(std::string name,
           int32_t node_pos, int32_t node_neg,
           int32_t node_ctrl_pos, int32_t node_ctrl_neg,
           double gm)
    : Device(std::move(name))
    , np_(node_pos)
    , nn_(node_neg)
    , ncp_(node_ctrl_pos)
    , ncn_(node_ctrl_neg)
    , gm_(gm)
{}

void VCCS::stamp_pattern(SparsityBuilder& builder) const {
    // KCL at output node np couples to control nodes
    stamp_if_not_ground(builder, np_, ncp_);
    stamp_if_not_ground(builder, np_, ncn_);
    // KCL at output node nn couples to control nodes
    stamp_if_not_ground(builder, nn_, ncp_);
    stamp_if_not_ground(builder, nn_, ncn_);
}

void VCCS::assign_offsets(const SparsityPattern& pattern) {
    off_np_ncp_ = offset_if_not_ground(pattern, np_,  ncp_);
    off_np_ncn_ = offset_if_not_ground(pattern, np_,  ncn_);
    off_nn_ncp_ = offset_if_not_ground(pattern, nn_,  ncp_);
    off_nn_ncn_ = offset_if_not_ground(pattern, nn_,  ncn_);
}

void VCCS::evaluate(const std::vector<double>& /*voltages*/,
                    NumericMatrix& mat, std::vector<double>& /*rhs*/) {
    // I_out = gm * (V(ncp) - V(ncn)) flows from np to nn
    //
    // KCL at np (current leaves np): contribution is -I_out
    //   d(-I_out)/d(V(ncp)) = -gm  → mat[np, ncp] += -gm
    //   d(-I_out)/d(V(ncn)) = +gm  → mat[np, ncn] += +gm
    //
    // KCL at nn (current enters nn): contribution is +I_out
    //   d(+I_out)/d(V(ncp)) = +gm  → mat[nn, ncp] += +gm
    //   d(+I_out)/d(V(ncn)) = -gm  → mat[nn, ncn] += -gm
    add_if_valid(mat, off_np_ncp_, -gm_);
    add_if_valid(mat, off_np_ncn_,  gm_);
    add_if_valid(mat, off_nn_ncp_,  gm_);
    add_if_valid(mat, off_nn_ncn_, -gm_);
}

void VCCS::ac_stamp(const std::vector<double>& /*voltages*/,
                    NumericMatrix& G, NumericMatrix& /*C*/) {
    // Linear device — AC stamp is identical to DC evaluate, into G matrix only
    add_if_valid(G, off_np_ncp_, -gm_);
    add_if_valid(G, off_np_ncn_,  gm_);
    add_if_valid(G, off_nn_ncp_,  gm_);
    add_if_valid(G, off_nn_ncn_, -gm_);
}

} // namespace neospice
