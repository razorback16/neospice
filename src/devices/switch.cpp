#include "devices/switch.hpp"
#include "core/circuit.hpp"   // tls_integrator_ctx
#include <stdexcept>

namespace neospice {

// Mode flag bits (ngspice cktdefs.h)
static constexpr int MODEINITFIX_BIT    = 0x400;
static constexpr int MODEINITJCT_BIT    = 0x200;
static constexpr int MODEINITFLOAT_BIT  = 0x100;
static constexpr int MODEINITSMSIG_BIT  = 0x800;
static constexpr int MODEINITTRAN_BIT   = 0x1000;
static constexpr int MODEINITPRED_BIT   = 0x2000;

// ---------------------------------------------------------------------------
// compute_switch_state — ngspice 4-state hysteresis algorithm
//
// Mirrors swload.c / cswload.c logic exactly.  The "init" phase corresponds
// to MODEINITFIX|MODEINITJCT; "float" to MODEINITFLOAT; "tran/pred" to
// MODEINITTRAN|MODEINITPRED.
// ---------------------------------------------------------------------------

static SwitchState compute_switch_state(double ctrl,
                                        double Vt, double Vh,
                                        SwitchState old_current_state,
                                        SwitchState previous_state,
                                        int mode,
                                        bool initial_on)
{
    SwitchState current_state = old_current_state;

    if (mode & (MODEINITFIX_BIT | MODEINITJCT_BIT)) {
        // Initialization phase
        if (initial_on) {
            // switch specified "on"
            if ((Vh >= 0) && (ctrl > (Vt + Vh)))
                current_state = SwitchState::REALLY_ON;
            else if ((Vh < 0) && (ctrl > (Vt - Vh)))
                current_state = SwitchState::REALLY_ON;
            else
                current_state = SwitchState::HYST_ON;
        } else {
            // switch specified "off" (default)
            if ((Vh >= 0) && (ctrl < (Vt - Vh)))
                current_state = SwitchState::REALLY_OFF;
            else if ((Vh < 0) && (ctrl < (Vt + Vh)))
                current_state = SwitchState::REALLY_OFF;
            else
                current_state = SwitchState::HYST_OFF;
        }
    } else if (mode & MODEINITSMSIG_BIT) {
        // Small-signal init: keep previous state
        current_state = previous_state;
    } else if (mode & MODEINITFLOAT_BIT) {
        // Corrector (Newton iteration) phase
        if (Vh > 0) {
            if (ctrl > (Vt + Vh))
                current_state = SwitchState::REALLY_ON;
            else if (ctrl < (Vt - Vh))
                current_state = SwitchState::REALLY_OFF;
            else
                current_state = old_current_state;
        } else if (Vh < 0) {
            // Negative hysteresis
            if (ctrl > (Vt - Vh))
                current_state = SwitchState::REALLY_ON;
            else if (ctrl < (Vt + Vh))
                current_state = SwitchState::REALLY_OFF;
            else {
                // In hysteresis region
                if (previous_state == SwitchState::HYST_OFF ||
                    previous_state == SwitchState::HYST_ON)
                    current_state = previous_state;
                else if (previous_state == SwitchState::REALLY_ON)
                    current_state = SwitchState::HYST_OFF;
                else if (previous_state == SwitchState::REALLY_OFF)
                    current_state = SwitchState::HYST_ON;
            }
        } else {
            // Vh == 0: simple comparator
            current_state = (ctrl > Vt) ? SwitchState::REALLY_ON
                                        : SwitchState::REALLY_OFF;
        }
    } else if (mode & (MODEINITTRAN_BIT | MODEINITPRED_BIT)) {
        // Predictor phase (first and subsequent transient steps)
        if (Vh > 0) {
            if (ctrl > (Vt + Vh))
                current_state = SwitchState::REALLY_ON;
            else if (ctrl < (Vt - Vh))
                current_state = SwitchState::REALLY_OFF;
            else
                current_state = previous_state;
        } else if (Vh < 0) {
            // Negative hysteresis
            if (ctrl > (Vt - Vh))
                current_state = SwitchState::REALLY_ON;
            else if (ctrl < (Vt + Vh))
                current_state = SwitchState::REALLY_OFF;
            else {
                if (previous_state == SwitchState::HYST_ON ||
                    previous_state == SwitchState::HYST_OFF)
                    current_state = previous_state;
                else if (previous_state == SwitchState::REALLY_ON)
                    current_state = SwitchState::REALLY_OFF;
                else if (previous_state == SwitchState::REALLY_OFF)
                    current_state = SwitchState::REALLY_ON;
            }
        } else {
            // Vh == 0: simple comparator
            current_state = (ctrl > Vt) ? SwitchState::REALLY_ON
                                        : SwitchState::REALLY_OFF;
        }
    }
    return current_state;
}

// ===========================================================================
// VSwitch — Voltage-controlled switch
// ===========================================================================

VSwitch::VSwitch(std::string name,
                 int32_t node_pos, int32_t node_neg,
                 int32_t node_ctrl_pos, int32_t node_ctrl_neg,
                 const SwitchModel& model,
                 bool initial_on)
    : Device(std::move(name))
    , np_(node_pos), nn_(node_neg)
    , ncp_(node_ctrl_pos), ncn_(node_ctrl_neg)
    , model_(model)
    , initial_on_(initial_on)
    , current_state_(initial_on ? SwitchState::HYST_ON : SwitchState::HYST_OFF)
    , previous_state_(current_state_)
{
    last_g_ = switch_is_on(current_state_) ? (1.0 / model_.Ron) : (1.0 / model_.Roff);
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
    // Read control voltage
    double Vcp = (ncp_ >= 0) ? voltages[ncp_] : 0.0;
    double Vcn = (ncn_ >= 0) ? voltages[ncn_] : 0.0;
    double v_ctrl = Vcp - Vcn;

    // Read mode from integrator context
    int mode = 0;
    if (tls_integrator_ctx)
        mode = tls_integrator_ctx->mode;

    // State rotation: at the start of a new transient step (predictor phase),
    // save current_state_ as previous_state_.  This mirrors ngspice's
    // CKTstates[0] -> CKTstates[1] rotation that happens before the Newton
    // loop for each timestep.  During DC init, bootstrap both to the same
    // value.
    if (mode & (MODEINITTRAN_BIT | MODEINITPRED_BIT)) {
        prev_state_changed_ = state_changed_;
        previous_state_ = current_state_;
    }

    SwitchState old_current = current_state_;

    SwitchState new_state = compute_switch_state(
        v_ctrl, model_.Vt, model_.Vh,
        old_current, previous_state_, mode, initial_on_);

    // Track state change for convergence (MODEINITFLOAT only, like ngspice)
    if (mode & MODEINITFLOAT_BIT)
        state_changed_ = (new_state != old_current);
    else
        state_changed_ = false;

    current_state_ = new_state;

    // During DC init, bootstrap previous_state_ = current_state_
    if (mode & (MODEINITFIX_BIT | MODEINITJCT_BIT))
        previous_state_ = current_state_;

    // Compute conductance
    double g = switch_is_on(current_state_) ? (1.0 / model_.Ron)
                                            : (1.0 / model_.Roff);
    last_g_ = g;

    // Stamp conductance
    add_if_valid(mat, off_pp_,  g);
    add_if_valid(mat, off_pn_, -g);
    add_if_valid(mat, off_np_, -g);
    add_if_valid(mat, off_nn_,  g);
}

void VSwitch::ac_stamp(const std::vector<double>& /*voltages*/,
                       NumericMatrix& G, NumericMatrix& /*C*/) {
    // Use the DC operating point conductance
    double g = last_g_;
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
                 const SwitchModel& model,
                 bool initial_on)
    : Device(std::move(name))
    , np_(node_pos), nn_(node_neg)
    , sense_(sense)
    , model_(model)
    , initial_on_(initial_on)
    , current_state_(initial_on ? SwitchState::HYST_ON : SwitchState::HYST_OFF)
    , previous_state_(current_state_)
{
    if (!sense_)
        throw std::invalid_argument("CSwitch: sense pointer must not be null");

    last_g_ = switch_is_on(current_state_) ? (1.0 / model_.Ron) : (1.0 / model_.Roff);
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
    // Read sense current
    int32_t bidx = sense_->branch_index();
    double i_ctrl = (bidx >= 0 && bidx < static_cast<int32_t>(voltages.size()))
                    ? voltages[bidx] : 0.0;

    // Read mode from integrator context
    int mode = 0;
    if (tls_integrator_ctx)
        mode = tls_integrator_ctx->mode;

    // State rotation: predictor phase saves current as previous
    if (mode & (MODEINITTRAN_BIT | MODEINITPRED_BIT)) {
        prev_state_changed_ = state_changed_;
        previous_state_ = current_state_;
    }

    SwitchState old_current = current_state_;

    SwitchState new_state = compute_switch_state(
        i_ctrl, model_.Vt, model_.Vh,
        old_current, previous_state_, mode, initial_on_);

    // Track state change for convergence (MODEINITFLOAT only)
    if (mode & MODEINITFLOAT_BIT)
        state_changed_ = (new_state != old_current);
    else
        state_changed_ = false;

    current_state_ = new_state;

    // During DC init, bootstrap previous_state_ = current_state_
    if (mode & (MODEINITFIX_BIT | MODEINITJCT_BIT))
        previous_state_ = current_state_;

    // Compute conductance
    double g = switch_is_on(current_state_) ? (1.0 / model_.Ron)
                                            : (1.0 / model_.Roff);
    last_g_ = g;

    // Stamp conductance
    add_if_valid(mat, off_pp_,  g);
    add_if_valid(mat, off_pn_, -g);
    add_if_valid(mat, off_np_, -g);
    add_if_valid(mat, off_nn_,  g);
}

void CSwitch::ac_stamp(const std::vector<double>& /*voltages*/,
                       NumericMatrix& G, NumericMatrix& /*C*/) {
    // Use the DC operating point conductance
    double g = last_g_;
    add_if_valid(G, off_pp_,  g);
    add_if_valid(G, off_pn_, -g);
    add_if_valid(G, off_np_, -g);
    add_if_valid(G, off_nn_,  g);
}

// ===========================================================================
// compute_trunc — reduce timestep after switch state changes
// ===========================================================================

double VSwitch::compute_trunc(const IntegratorCtx& ctx,
                              const SimOptions& /*opts*/) const {
    if (prev_state_changed_) {
        return 0.1 * ctx.delta;
    }
    return 1e30;
}

double CSwitch::compute_trunc(const IntegratorCtx& ctx,
                              const SimOptions& /*opts*/) const {
    if (prev_state_changed_) {
        return 0.1 * ctx.delta;
    }
    return 1e30;
}

} // namespace neospice
