#include "devices/capacitor.hpp"
#include <cmath>
#include <algorithm>

namespace neospice {

Capacitor::Capacitor(std::string name, int32_t node_pos, int32_t node_neg, double capacitance)
    : Device(std::move(name)), np_(node_pos), nn_(node_neg), cap_(capacitance)
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

void Capacitor::evaluate(const std::vector<double>& /*voltages*/,
                         NumericMatrix& mat, std::vector<double>& rhs) {
    if (!transient_) return;

    double g_eq, i_eq;

    if (integration_method_ == 1 && gear_ready_) {
        // Gear BDF-2 with variable-timestep coefficients
        // From ngspice NIcomCof: r = dt_prev/dt
        //   ag0 = (2+r)/((1+r)*dt)
        //   ag1 = -(1+r)/(r*dt)
        //   ag2 = 1/((1+r)*r*dt)
        double r = (dt_prev_ > 0.0) ? dt_prev_ / dt_ : 1.0;
        double ag0 = (2.0 + r) / ((1.0 + r) * dt_);
        g_eq = ag0 * cap_;
        double ag1 = -(1.0 + r) / (r * dt_);
        double ag2 = 1.0 / ((1.0 + r) * r * dt_);
        i_eq = -cap_ * (ag1 * v_prev_ + ag2 * v_prev2_);
    } else {
        // Trapezoidal
        g_eq = 2.0 * cap_ / dt_;
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
    add_if_valid(C, off_pp_,  cap_);
    add_if_valid(C, off_pn_, -cap_);
    add_if_valid(C, off_np_, -cap_);
    add_if_valid(C, off_nn_,  cap_);
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
    q_prev_ = cap_ * v_prev_;
    dt_prev_ = dt_;

    if (integration_method_ == 1 && gear_ready_) {
        // Variable-timestep BDF-2 current: i = C*(ag0*v + ag1*v_prev + ag2*v_prev2)
        double r = (dt_prev_ > 0.0) ? dt_prev_ / dt_ : 1.0;
        double ag0 = (2.0 + r) / ((1.0 + r) * dt_);
        double ag1 = -(1.0 + r) / (r * dt_);
        double ag2 = 1.0 / ((1.0 + r) * r * dt_);
        double i_new = cap_ * (ag0 * v_across + ag1 * v_prev_ + ag2 * v_prev2_);
        v_prev2_ = v_prev_;
        i_prev2_ = i_prev_;
        v_prev_ = v_across;
        i_prev_ = i_new;
    } else {
        double g_eq = 2.0 * cap_ / dt_;
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
    q_prev_ = cap_ * v_prev_;
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
    if (!transient_ || ctx.order < 2 || ctx.delta <= 0.0)
        return 1e30;

    const double h0 = ctx.delta;              // current timestep
    const double h1 = ctx.delta_old[1];       // previous timestep
    if (h1 <= 0.0) return 1e30;

    // Current charge and current (dQ/dt companion model)
    double q0 = cap_ * v_prev_;    // charge at current accepted solution
    // Note: q_prev_ was saved BEFORE v_prev_ was updated, so it holds Q(n-1)
    double q1 = q_prev_;           // charge at previous step
    double q2 = q_prev2_;          // charge at two steps ago

    // Second divided difference of charge (CKTterr divided-difference loop)
    double dd1 = (q0 - q1) / h0;
    double dd2 = ((q0 - q1) / h0 - (q1 - q2) / h1) / (h0 + h1);

    // Tolerance (matches CKTterr):
    //   volttol = abstol + reltol * max(|ccap0|, |ccap1|)
    //   chargetol = reltol * max(|q0|, chgtol) / delta
    //   tol = max(volttol, chargetol)
    // The current (ccap) is i_prev_ after the step was accepted.
    double volttol = opts.abstol + opts.reltol *
                     std::max(std::abs(i_prev_), std::abs(dd1));
    double chargetol = opts.reltol *
                       std::max(std::abs(q0), opts.chgtol) / h0;
    double tol = std::max(volttol, chargetol);
    if (tol <= 0.0) return 1e30;

    // LTE coefficient: trap order 2 = 1/12, gear order 2 = 2/9
    const double lte_coeff = ctx.lte_coefficient();

    double lte_abs = lte_coeff * std::abs(dd2);
    if (lte_abs <= opts.abstol) return 1e30;   // ngspice: max(abstol, factor*|diff|)

    double del = opts.trtol * tol / lte_abs;
    del = std::sqrt(del);    // order == 2

    return del;
}

} // namespace neospice
