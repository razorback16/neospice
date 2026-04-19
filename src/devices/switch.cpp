#include "devices/switch.hpp"
#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace neospice {

// ===========================================================================
// VSwitch — Voltage-controlled switch
// ===========================================================================

VSwitch::VSwitch(std::string name,
                 int32_t node_pos, int32_t node_neg,
                 int32_t node_ctrl_pos, int32_t node_ctrl_neg,
                 const SwitchModel& model)
    : Device(std::move(name))
    , np_(node_pos), nn_(node_neg)
    , ncp_(node_ctrl_pos), ncn_(node_ctrl_neg)
    , model_(model)
{
    // Initialize last_g_ to off-state conductance
    last_g_ = 1.0 / model_.Roff;
}

double VSwitch::compute_conductance(const std::vector<double>& voltages) const {
    // Compute control voltage: Vc = V(nc+) - V(nc-)
    double Vcp = (ncp_ >= 0) ? voltages[ncp_] : 0.0;
    double Vcn = (ncn_ >= 0) ? voltages[ncn_] : 0.0;
    double Vc = Vcp - Vcn;

    double Vh = std::max(std::abs(model_.Vh), 1e-12);
    double x = (Vc - model_.Vt) / Vh;
    double f = switch_smooth_step(x);

    double Gon  = 1.0 / model_.Ron;
    double Goff = 1.0 / model_.Roff;
    return Goff + (Gon - Goff) * f;
}

void VSwitch::stamp_pattern(SparsityBuilder& builder) const {
    stamp_if_not_ground(builder, np_, np_);
    stamp_if_not_ground(builder, np_, nn_);
    stamp_if_not_ground(builder, nn_, np_);
    stamp_if_not_ground(builder, nn_, nn_);
}

void VSwitch::assign_offsets(const SparsityPattern& pattern) {
    off_pp_ = offset_if_not_ground(pattern, np_, np_);
    off_pn_ = offset_if_not_ground(pattern, np_, nn_);
    off_np_ = offset_if_not_ground(pattern, nn_, np_);
    off_nn_ = offset_if_not_ground(pattern, nn_, nn_);
}

void VSwitch::evaluate(const std::vector<double>& voltages,
                       NumericMatrix& mat, std::vector<double>& /*rhs*/) {
    last_g_ = compute_conductance(voltages);
    add_if_valid(mat, off_pp_,  last_g_);
    add_if_valid(mat, off_pn_, -last_g_);
    add_if_valid(mat, off_np_, -last_g_);
    add_if_valid(mat, off_nn_,  last_g_);
}

void VSwitch::ac_stamp(const std::vector<double>& voltages,
                       NumericMatrix& G, NumericMatrix& /*C*/) {
    // Linearized around the DC operating point: use last computed conductance
    // If voltages are available (called after DC solve), recompute.
    double g = compute_conductance(voltages);
    last_g_ = g;
    add_if_valid(G, off_pp_,  g);
    add_if_valid(G, off_pn_, -g);
    add_if_valid(G, off_np_, -g);
    add_if_valid(G, off_nn_,  g);
}

// ===========================================================================
// CSwitch — Current-controlled switch
// ===========================================================================

CSwitch::CSwitch(std::string name,
                 int32_t node_pos, int32_t node_neg,
                 const VSource* sense,
                 const SwitchModel& model)
    : Device(std::move(name))
    , np_(node_pos), nn_(node_neg)
    , sense_(sense)
    , model_(model)
{
    if (!sense_)
        throw std::invalid_argument("CSwitch: sense pointer must not be null");

    // Initialize last_g_ to off-state conductance
    last_g_ = 1.0 / model_.Roff;
}

double CSwitch::compute_conductance(const std::vector<double>& voltages) const {
    // The branch current of the sense VSource is stored at its branch index
    int32_t bidx = sense_->branch_index();
    double Ic = (bidx >= 0 && bidx < static_cast<int32_t>(voltages.size()))
                ? voltages[bidx] : 0.0;

    double Ih = std::max(std::abs(model_.Vh), 1e-12);
    double x = (Ic - model_.Vt) / Ih;
    double f = switch_smooth_step(x);

    double Gon  = 1.0 / model_.Ron;
    double Goff = 1.0 / model_.Roff;
    return Goff + (Gon - Goff) * f;
}

void CSwitch::stamp_pattern(SparsityBuilder& builder) const {
    stamp_if_not_ground(builder, np_, np_);
    stamp_if_not_ground(builder, np_, nn_);
    stamp_if_not_ground(builder, nn_, np_);
    stamp_if_not_ground(builder, nn_, nn_);
}

void CSwitch::assign_offsets(const SparsityPattern& pattern) {
    off_pp_ = offset_if_not_ground(pattern, np_, np_);
    off_pn_ = offset_if_not_ground(pattern, np_, nn_);
    off_np_ = offset_if_not_ground(pattern, nn_, np_);
    off_nn_ = offset_if_not_ground(pattern, nn_, nn_);
}

void CSwitch::evaluate(const std::vector<double>& voltages,
                       NumericMatrix& mat, std::vector<double>& /*rhs*/) {
    last_g_ = compute_conductance(voltages);
    add_if_valid(mat, off_pp_,  last_g_);
    add_if_valid(mat, off_pn_, -last_g_);
    add_if_valid(mat, off_np_, -last_g_);
    add_if_valid(mat, off_nn_,  last_g_);
}

void CSwitch::ac_stamp(const std::vector<double>& voltages,
                       NumericMatrix& G, NumericMatrix& /*C*/) {
    double g = compute_conductance(voltages);
    last_g_ = g;
    add_if_valid(G, off_pp_,  g);
    add_if_valid(G, off_pn_, -g);
    add_if_valid(G, off_np_, -g);
    add_if_valid(G, off_nn_,  g);
}

} // namespace neospice
