#include "devices/vccs.hpp"
#include "core/circuit.hpp"   // tls_integrator_ctx

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
    // SPICE convention: I = gm * (V(ncp) - V(ncn)) leaves N+ (np).
    // Current leaving np = +gm*(V(ncp)-V(ncn))  → mat[np,ncp] += +gm
    // Current leaving nn = -gm*(V(ncp)-V(ncn))  → mat[nn,ncp] += -gm
    double gm = gm_ * m_;
    // Scale by dep_src_fact for gain stepping convergence aid
    if (tls_integrator_ctx && tls_integrator_ctx->options)
        gm *= tls_integrator_ctx->options->dep_src_fact;
    add_if_valid(mat, off_np_ncp_,  gm);
    add_if_valid(mat, off_np_ncn_, -gm);
    add_if_valid(mat, off_nn_ncp_, -gm);
    add_if_valid(mat, off_nn_ncn_,  gm);
}

void VCCS::ac_stamp(const std::vector<double>& /*voltages*/,
                    NumericMatrix& G, NumericMatrix& /*C*/) {
    const double gm = gm_ * m_;
    add_if_valid(G, off_np_ncp_,  gm);
    add_if_valid(G, off_np_ncn_, -gm);
    add_if_valid(G, off_nn_ncp_, -gm);
    add_if_valid(G, off_nn_ncn_,  gm);
}

} // namespace neospice
