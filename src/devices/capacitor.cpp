#include "devices/capacitor.hpp"
#include "core/circuit.hpp"
#include <cmath>
#include <algorithm>

namespace neospice {

Capacitor::Capacitor(std::string name, int32_t node_pos, int32_t node_neg, double capacitance)
    : Device(std::move(name)), np_(node_pos), nn_(node_neg),
      cap_nom_(capacitance), cap_eff_(capacitance)
{}

void Capacitor::stamp_pattern(SparsityBuilder& builder) const {
    stamp_if_not_ground(builder, np_, np_);
    stamp_if_not_ground(builder, np_, nn_);
    stamp_if_not_ground(builder, nn_, np_);
    stamp_if_not_ground(builder, nn_, nn_);
}

void Capacitor::assign_offsets(const SparsityPattern& pattern) {
    off_pp_ = offset_if_not_ground(pattern, np_, np_);
    off_pn_ = offset_if_not_ground(pattern, np_, nn_);
    off_np_ = offset_if_not_ground(pattern, nn_, np_);
    off_nn_ = offset_if_not_ground(pattern, nn_, nn_);
}

void Capacitor::evaluate(const std::vector<double>& voltages,
                         NumericMatrix& mat, std::vector<double>& rhs) {
    if (state0_) {
        // State-vector path (matches ngspice capload.c)
        const IntegratorCtx* ic = tls_integrator_ctx;
        if (!ic) return;

        constexpr int MODETRAN_BIT     = 0x1;
        constexpr int MODEAC_BIT       = 0x2;
        constexpr int MODETRANOP_BIT   = 0x20;
        constexpr int MODEUIC_BIT      = 0x10000;
        constexpr int MODEINITPRED_BIT = 0x2000;
        constexpr int MODEINITTRAN_BIT = 0x1000;
        constexpr int MODEINITJCT_BIT  = 0x200;
        constexpr int MODEDC_BIT       = 0x70;

        if (!(ic->mode & (MODETRAN_BIT | MODEAC_BIT | MODETRANOP_BIT)))
            return;

        bool cond1 = ((ic->mode & MODEDC_BIT) && (ic->mode & MODEINITJCT_BIT))
                   || ((ic->mode & MODEUIC_BIT) && (ic->mode & MODEINITTRAN_BIT));

        double vcap;
        if (cond1) {
            vcap = has_ic() ? ic_ : 0.0;
        } else {
            double vp = (np_ >= 0 ? voltages[np_] : 0.0);
            double vn = (nn_ >= 0 ? voltages[nn_] : 0.0);
            vcap = vp - vn;
        }

        if (ic->mode & (MODETRAN_BIT | MODEAC_BIT)) {
            if (ic->mode & MODEINITPRED_BIT) {
                state0_[0] = state1_[0];
            } else {
                state0_[0] = cap_eff_ * vcap;
                if (ic->mode & MODEINITTRAN_BIT) {
                    state1_[0] = state0_[0];
                }
            }

            // NIintegrate inline (matches bsim4v7_shim.cpp and ngspice niinteg.c)
            int order = ic->order;
            if (order < 1) order = 1;
            if (order > 2) order = 2;

            double deriv;
            if (order == 1) {
                deriv = ic->ag[0] * state0_[0] + ic->ag[1] * state1_[0];
            } else if (ic->integrate_method == 0) {
                // Trapezoidal
                deriv = -state1_[1] + ic->ag[0] * state0_[0] + ic->ag[1] * state1_[0];
            } else {
                // Gear BDF-2
                deriv = ic->ag[0] * state0_[0] + ic->ag[1] * state1_[0]
                      + ic->ag[2] * state2_[0];
            }
            state0_[1] = deriv;

            double geq = ic->ag[0] * cap_eff_;
            double ceq = state0_[1] - ic->ag[0] * state0_[0];

            if (ic->mode & MODEINITTRAN_BIT) {
                state1_[1] = state0_[1];
            }

            double m = m_;
            add_if_valid(mat, off_pp_,  m * geq);
            add_if_valid(mat, off_pn_, -m * geq);
            add_if_valid(mat, off_np_, -m * geq);
            add_if_valid(mat, off_nn_,  m * geq);

            add_rhs_if_valid(rhs, np_, -m * ceq);
            add_rhs_if_valid(rhs, nn_,  m * ceq);
        } else {
            state0_[0] = cap_eff_ * vcap;
        }
        return;
    }

    // Legacy path: explicit companion model (for standalone unit tests)
    if (!transient_) return;

    double g_eq, i_eq;

    if (integration_method_ == 1 && gear_ready_) {
        double r = (dt_prev_ > 0.0) ? dt_prev_ / dt_ : 1.0;
        double ag0 = (2.0 + r) / ((1.0 + r) * dt_);
        g_eq = ag0 * cap_eff_;
        double ag1 = -(1.0 + r) / (r * dt_);
        double ag2 = 1.0 / ((1.0 + r) * r * dt_);
        i_eq = -cap_eff_ * (ag1 * v_prev_ + ag2 * v_prev2_);
    } else if (integrator_order_ <= 1) {
        g_eq = cap_eff_ / dt_;
        i_eq = g_eq * v_prev_;
    } else {
        g_eq = 2.0 * cap_eff_ / dt_;
        i_eq = g_eq * v_prev_ + i_prev_;
    }

    add_if_valid(mat, off_pp_,  g_eq);
    add_if_valid(mat, off_pn_, -g_eq);
    add_if_valid(mat, off_np_, -g_eq);
    add_if_valid(mat, off_nn_,  g_eq);

    add_rhs_if_valid(rhs, np_,  i_eq);
    add_rhs_if_valid(rhs, nn_, -i_eq);
}

void Capacitor::ac_stamp(const std::vector<double>& /*voltages*/,
                         NumericMatrix& /*G*/, NumericMatrix& C) {
    // Capacitance stamps into C matrix only; G matrix gets no contribution
    add_if_valid(C, off_pp_,  cap_eff_);
    add_if_valid(C, off_pn_, -cap_eff_);
    add_if_valid(C, off_np_, -cap_eff_);
    add_if_valid(C, off_nn_,  cap_eff_);
}

void Capacitor::set_transient(double dt) {
    transient_ = true;
    dt_ = dt;
}

void Capacitor::clear_transient() {
    transient_ = false;
    dt_ = 0.0;
    gear_ready_ = false;
}

void Capacitor::set_integration_method(int method) {
    integration_method_ = method;
}

void Capacitor::accept_step(double v_across) {
    // Track charge history for LTE (before v_prev_ is overwritten)
    q_prev3_ = q_prev2_;
    q_prev2_ = q_prev_;
    q_prev_ = cap_eff_ * v_prev_;
    dt_prev_ = dt_;

    if (integration_method_ == 1 && gear_ready_) {
        // Variable-timestep BDF-2 current: i = C*(ag0*v + ag1*v_prev + ag2*v_prev2)
        double r = (dt_prev_ > 0.0) ? dt_prev_ / dt_ : 1.0;
        double ag0 = (2.0 + r) / ((1.0 + r) * dt_);
        double ag1 = -(1.0 + r) / (r * dt_);
        double ag2 = 1.0 / ((1.0 + r) * r * dt_);
        double i_new = cap_eff_ * (ag0 * v_across + ag1 * v_prev_ + ag2 * v_prev2_);
        v_prev2_ = v_prev_;
        i_prev2_ = i_prev_;
        v_prev_ = v_across;
        i_prev_ = i_new;
    } else if (integrator_order_ <= 1) {
        // Backward Euler current
        double g_eq = cap_eff_ / dt_;
        double i_new = g_eq * (v_across - v_prev_);
        v_prev2_ = v_prev_;
        i_prev2_ = i_prev_;
        v_prev_ = v_across;
        i_prev_ = i_new;
        if (integration_method_ == 1) {
            gear_ready_ = true;
        }
    } else {
        // Trapezoidal current (order 2)
        double g_eq = 2.0 * cap_eff_ / dt_;
        double i_eq = g_eq * v_prev_ + i_prev_;
        double i_new = g_eq * v_across - i_eq;
        v_prev2_ = v_prev_;
        i_prev2_ = i_prev_;
        v_prev_ = v_across;
        i_prev_ = i_new;
        if (integration_method_ == 1) {
            gear_ready_ = true;
        }
    }
}

void Capacitor::accept_step_from_solution(const std::vector<double>& sol) {
    double va = (np_ >= 0) ? sol[np_] : 0.0;
    double vc = (nn_ >= 0) ? sol[nn_] : 0.0;
    accept_step(va - vc);
}

void Capacitor::init_dc_state(const std::vector<double>& sol) {
    double va = (np_ >= 0) ? sol[np_] : 0.0;
    double vc = (nn_ >= 0) ? sol[nn_] : 0.0;
    v_prev_ = va - vc;
    i_prev_ = 0.0;
    v_prev2_ = v_prev_;
    i_prev2_ = 0.0;
    gear_ready_ = false;

    // Initialize charge history
    q_prev_ = cap_eff_ * v_prev_;
    q_prev2_ = q_prev_;
    q_prev3_ = q_prev_;
}

void Capacitor::init_dc_state_gear(double v_prev, double i_prev,
                                    double v_prev2, double i_prev2) {
    v_prev_ = v_prev;
    i_prev_ = i_prev;
    v_prev2_ = v_prev2;
    i_prev2_ = i_prev2;
    gear_ready_ = true;
}

// ---------------------------------------------------------------------------
// compute_trunc — LTE-based timestep control for capacitor charge
//
// Mirrors ngspice CKTterr(CAPqcap, ckt, timeStep) in cktterr.c.
// Uses divided differences of charge history to estimate local truncation
// error, then limits the next timestep so LTE stays within tolerance.
// ---------------------------------------------------------------------------
double Capacitor::compute_trunc(const IntegratorCtx& ctx,
                                const SimOptions& opts) const {
    if (ctx.order < 2 || ctx.delta <= 0.0) return 1e30;

    const double h0 = ctx.delta;
    const double h1 = ctx.delta_old[1];
    if (h1 <= 0.0) return 1e30;

    double q0, q1, q2, ccap0, ccap1;

    if (state0_) {
        q0 = state0_[0];
        q1 = state1_[0];
        q2 = state2_[0];
        ccap0 = state0_[1];
        ccap1 = state1_[1];
    } else {
        if (!transient_) return 1e30;
        q0 = cap_eff_ * v_prev_;
        q1 = q_prev_;
        q2 = q_prev2_;
        ccap0 = i_prev_;
        ccap1 = ccap0;  // approximate
    }

    double dd1 = (q0 - q1) / h0;
    double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);

    double volttol = opts.abstol + opts.reltol *
                     std::max(std::abs(ccap0), std::abs(ccap1));
    double chargetol = opts.reltol *
                       std::max(std::abs(q0), opts.chgtol) / h0;
    double tol = std::max(volttol, chargetol);
    if (tol <= 0.0) return 1e30;

    const double lte_coeff = ctx.lte_coefficient();
    double lte_abs = lte_coeff * std::abs(dd2);
    if (lte_abs <= opts.abstol) return 1e30;

    double del = opts.trtol * tol / lte_abs;
    del = std::sqrt(del);
    return del;
}

void Capacitor::apply_ic_override() {
    if (!has_ic()) return;
    v_prev_ = ic_;
    v_prev2_ = ic_;
    i_prev_ = 0.0;
    i_prev2_ = 0.0;
    q_prev_ = cap_eff_ * ic_;
    q_prev2_ = q_prev_;
    q_prev3_ = q_prev_;
}

void Capacitor::process_temperature(double sim_temp, double sim_tnom) {
    // Follows ngspice captemp.c exactly:
    //   difference = (device_temp + dtemp) - tnom
    //   factor = 1 + tc1*diff + tc2*diff^2
    //   capac = capac * factor * scale
    double tnom = sim_tnom;
    double temp = (temp_ > 0) ? temp_ : sim_temp;
    double dtemp = (temp_ > 0) ? 0.0 : dtemp_;
    double difference = (temp + dtemp) - tnom;
    double factor = 1.0 + tc1_ * difference + tc2_ * difference * difference;
    cap_eff_ = cap_nom_ * factor * scale_ * m_;
}

void Capacitor::set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) {
    state0_ = s0 + base;
    state1_ = s1 + base;
    state2_ = s2 + base;
    state_base_ = base;
}

} // namespace neospice
