#include "devices/switch.hpp"
#include <span>
#include "core/circuit.hpp"   // tls_integrator_ctx
#include "devices/xspice_input_limiter.hpp"
#include "core/ckt_mode.hpp"
#include <cmath>
#include <stdexcept>

namespace neospice {

// PSpice-style smooth switch conductance using cubic Hermite interpolation.
static double smooth_conductance_value(double ctrl, double Von, double Voff,
                                       double Ron, double Roff) {
    if (Von == Voff)
        return 1.0 / ((ctrl >= Von) ? Ron : Roff);

    // cm_pswitch computes a cubic in log(resistance), not log(conductance).
    // Keep its operation order verbatim because the two algebraically
    // equivalent forms round differently at the last bit.
    double r;
    if (Von > Voff) {
        if (ctrl > Von) return 1.0 / Ron;
        if (ctrl < Voff) return 1.0 / Roff;
    } else {
        if (ctrl < Von) return 1.0 / Ron;
        if (ctrl > Voff) return 1.0 / Roff;
    }
    const double cntl_diff = (Von > Voff) ? 1.0 : -1.0;
    const double inmean = (Von > Voff)
        ? (ctrl - Voff) / (Von - Voff) - 0.5
        : (Von - ctrl) / (Von - Voff) - 0.5;
    const double logmean = std::log(std::sqrt(Ron * Roff));
    const double logratio = std::log(Ron / Roff);
    const double c1 = 1.5 * logratio / cntl_diff;
    const double c3 = 2.0 * logratio /
        (cntl_diff * cntl_diff * cntl_diff);
    r = std::exp(logmean + c1 * inmean - c3 * inmean * inmean * inmean);
    if (r < Ron) r = Ron;
    return 1.0 / r;
}

static double smooth_conductance(double ctrl, double Von, double Voff,
                                 double Ron, double Roff, double& dg_dctrl) {
    const double g = smooth_conductance_value(ctrl, Von, Voff, Ron, Roff);
    // cm_pswitch calls cm_analog_auto_partial(), whose voltage-port rule is a
    // forward divided difference with a fixed 1e-6 V perturbation.  This is
    // observably different from the analytic derivative at the transition
    // boundaries and in severely ill-conditioned vendor macromodels.
    constexpr double epsilon = 1e-6;
    const double perturbed =
        smooth_conductance_value(ctrl + epsilon, Von, Voff, Ron, Roff);
    dg_dctrl = (perturbed - g) / epsilon;
    return g;
}

// Mode flag bits (ngspice cktdefs.h)

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
    if (model_.control_input_resistance > 0.0) {
        stamp_if_not_ground(builder, ncp_, ncp_);
        stamp_if_not_ground(builder, ncp_, ncn_);
        stamp_if_not_ground(builder, ncn_, ncp_);
        stamp_if_not_ground(builder, ncn_, ncn_);
        stamp_if_not_ground(builder, np_, ncp_);
        stamp_if_not_ground(builder, np_, ncn_);
        stamp_if_not_ground(builder, nn_, ncp_);
        stamp_if_not_ground(builder, nn_, ncn_);
        // XSPICE allocates the complete input/output Jacobian block for the
        // pswitch code model.  Its input current is independent of output
        // voltage, so these entries remain numeric zero, but they are still
        // structural elements and affect Sparse's Markowitz ordering.
        stamp_if_not_ground(builder, ncp_, np_);
        stamp_if_not_ground(builder, ncp_, nn_);
        stamp_if_not_ground(builder, ncn_, np_);
        stamp_if_not_ground(builder, ncn_, nn_);
    }
}

void VSwitch::assign_offsets(const SparsityPattern& pattern) {
    off_pp_ = offset_if_not_ground(pattern, np_, np_);
    off_pn_ = offset_if_not_ground(pattern, np_, nn_);
    off_np_ = offset_if_not_ground(pattern, nn_, np_);
    off_nn_ = offset_if_not_ground(pattern, nn_, nn_);
    if (model_.control_input_resistance > 0.0) {
        off_cp_cp_ = offset_if_not_ground(pattern, ncp_, ncp_);
        off_cp_cn_ = offset_if_not_ground(pattern, ncp_, ncn_);
        off_cn_cp_ = offset_if_not_ground(pattern, ncn_, ncp_);
        off_cn_cn_ = offset_if_not_ground(pattern, ncn_, ncn_);
        off_np_cp_ = offset_if_not_ground(pattern, np_, ncp_);
        off_np_cn_ = offset_if_not_ground(pattern, np_, ncn_);
        off_nn_cp_ = offset_if_not_ground(pattern, nn_, ncp_);
        off_nn_cn_ = offset_if_not_ground(pattern, nn_, ncn_);
    }
}

void VSwitch::evaluate(const std::vector<double>& voltages,
                       NumericMatrix& mat, std::span<double> rhs) {
    // Read control voltage
    double Vcp = (ncp_ >= 0) ? voltages[ncp_] : 0.0;
    double Vcn = (ncn_ >= 0) ? voltages[ncn_] : 0.0;
    double v_ctrl = Vcp - Vcn;
    const double vp = (np_ >= 0) ? voltages[np_] : 0.0;
    const double vn = (nn_ >= 0) ? voltages[nn_] : 0.0;
    double vout = vp - vn;

    double g;
    double dg_dctrl = 0.0;
    if (model_.smooth) {
        // pswitch exposes both its control and resistive-output ports as
        // analog inputs. MIFload limits each port to 25% of its previous
        // magnitude (at least 0.1 V) and rejects the iteration when clipped.
        // The persistent input values are deliberately not continuation
        // checkpoints; ngspice restores state0 but retains these port values.
        std::vector<double> inputs{v_ctrl, vout};
        inputs_limited_ = xspice_limit_analog_inputs(
            inputs, last_inputs_, input_state0_, input_state1_);
        v_ctrl = inputs[0];
        vout = inputs[1];
        g = smooth_conductance(v_ctrl, model_.Von, model_.Voff,
                               model_.Ron, model_.Roff, dg_dctrl);
        state_changed_ = false;
    } else {
        // Read mode from integrator context
        int mode = 0;
        if (tls_integrator_ctx)
            mode = tls_integrator_ctx->mode;

        if (mode & (MODEINITTRAN_BIT | MODEINITPRED_BIT)) {
            prev_state_changed_ = state_changed_;
            previous_state_ = current_state_;
        }

        SwitchState old_current = current_state_;

        SwitchState new_state = compute_switch_state(
            v_ctrl, model_.Vt, model_.Vh,
            old_current, previous_state_, mode, initial_on_);

        if (mode & MODEINITFLOAT_BIT)
            state_changed_ = (new_state != old_current);
        else
            state_changed_ = false;

        current_state_ = new_state;

        if (mode & (MODEINITFIX_BIT | MODEINITJCT_BIT))
            previous_state_ = current_state_;

        g = switch_is_on(current_state_) ? (1.0 / model_.Ron)
                                         : (1.0 / model_.Roff);
    }
    last_g_ = g;

    double output_jacobian = g;
    double control_jacobian = 0.0;
    double companion_rhs = 0.0;
    if (model_.smooth) {
        // XSPICE code models obtain every analog partial through
        // cm_analog_auto_partial's 1 uV forward difference, including the
        // otherwise-linear output-voltage partial.  Retain that arithmetic:
        // its last-bit rounding is observable in Sparse's pivot selection for
        // ill-conditioned PSpice power-device macromodels.
        constexpr double epsilon = 1e-6;
        const double output = vout * g;
        output_jacobian = ((vout + epsilon) * g - output) / epsilon;
        const double perturbed_g = smooth_conductance_value(
            v_ctrl + epsilon, model_.Von, model_.Voff,
            model_.Ron, model_.Roff);
        control_jacobian = (vout * perturbed_g - output) / epsilon;
        companion_rhs = output_jacobian * vout
                      + control_jacobian * v_ctrl - output;
    }

    // Stamp conductance
    add_if_valid(mat, off_pp_,  output_jacobian);
    add_if_valid(mat, off_pn_, -output_jacobian);
    add_if_valid(mat, off_np_, -output_jacobian);
    add_if_valid(mat, off_nn_,  output_jacobian);

    // XSPICE pswitch is a genuinely nonlinear four-terminal device: output
    // current depends on both output voltage and control voltage.  Stamp the
    // control derivative and its Newton companion, not merely a frozen
    // iteration-to-iteration conductance.
    if (model_.smooth) {
        const double jctrl = control_jacobian;
        add_if_valid(mat, off_np_cp_,  jctrl);
        add_if_valid(mat, off_np_cn_, -jctrl);
        add_if_valid(mat, off_nn_cp_, -jctrl);
        add_if_valid(mat, off_nn_cn_,  jctrl);
        add_rhs_if_valid(rhs, np_,  companion_rhs);
        add_rhs_if_valid(rhs, nn_, -companion_rhs);
    }
    if (model_.control_input_resistance > 0.0) {
        constexpr double epsilon = 1e-6;
        const double control = v_ctrl;
        const double current = control / model_.control_input_resistance;
        const double gc = ((control + epsilon) /
                           model_.control_input_resistance - current) / epsilon;
        add_if_valid(mat, off_cp_cp_,  gc);
        add_if_valid(mat, off_cp_cn_, -gc);
        add_if_valid(mat, off_cn_cp_, -gc);
        add_if_valid(mat, off_cn_cn_,  gc);
    }
}

void VSwitch::ac_stamp(const std::vector<double>& /*voltages*/,
                       NumericMatrix& G, NumericMatrix& /*C*/) {
    // Use the DC operating point conductance
    double g = last_g_;
    add_if_valid(G, off_pp_,  g);
    add_if_valid(G, off_pn_, -g);
    add_if_valid(G, off_np_, -g);
    add_if_valid(G, off_nn_,  g);
    if (model_.control_input_resistance > 0.0) {
        const double gc = 1.0 / model_.control_input_resistance;
        add_if_valid(G, off_cp_cp_,  gc);
        add_if_valid(G, off_cp_cn_, -gc);
        add_if_valid(G, off_cn_cp_, -gc);
        add_if_valid(G, off_cn_cn_,  gc);
    }
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
                       NumericMatrix& mat, std::span<double> /*rhs*/) {
    // Read sense current
    int32_t bidx = sense_->branch_index();
    double i_ctrl = (bidx >= 0 && bidx < static_cast<int32_t>(voltages.size()))
                    ? voltages[bidx] : 0.0;

    double g;
    if (model_.smooth) {
        double unused_dg = 0.0;
        g = smooth_conductance(i_ctrl, model_.Von, model_.Voff,
                               model_.Ron, model_.Roff, unused_dg);
        state_changed_ = false;
    } else {
        int mode = 0;
        if (tls_integrator_ctx)
            mode = tls_integrator_ctx->mode;

        if (mode & (MODEINITTRAN_BIT | MODEINITPRED_BIT)) {
            prev_state_changed_ = state_changed_;
            previous_state_ = current_state_;
        }

        SwitchState old_current = current_state_;

        SwitchState new_state = compute_switch_state(
            i_ctrl, model_.Vt, model_.Vh,
            old_current, previous_state_, mode, initial_on_);

        if (mode & MODEINITFLOAT_BIT)
            state_changed_ = (new_state != old_current);
        else
            state_changed_ = false;

        current_state_ = new_state;

        if (mode & (MODEINITFIX_BIT | MODEINITJCT_BIT))
            previous_state_ = current_state_;

        g = switch_is_on(current_state_) ? (1.0 / model_.Ron)
                                         : (1.0 / model_.Roff);
    }
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
