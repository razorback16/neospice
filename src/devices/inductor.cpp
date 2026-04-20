#include "devices/inductor.hpp"
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace neospice {

Inductor::Inductor(std::string name, int32_t node_pos, int32_t node_neg, double inductance)
    : Device(std::move(name)), np_(node_pos), nn_(node_neg),
      inductance_nom_(inductance), inductance_eff_(inductance)
{}

void Inductor::set_branch_index(int32_t idx) {
    branch_idx_ = idx;
}

void Inductor::stamp_pattern(SparsityBuilder& builder) const {
    if (branch_idx_ < 0)
        throw std::logic_error("Inductor::stamp_pattern called before set_branch_index");
    // KCL coupling: (np, branch) and (nn, branch)
    stamp_if_not_ground(builder, np_, branch_idx_);
    stamp_if_not_ground(builder, nn_, branch_idx_);
    // Branch equation: (branch, np) and (branch, nn)
    stamp_if_not_ground(builder, branch_idx_, np_);
    stamp_if_not_ground(builder, branch_idx_, nn_);
    // (branch, branch) — always needed for transient R_eq term
    builder.add(branch_idx_, branch_idx_);
}

void Inductor::assign_offsets(const SparsityPattern& pattern) {
    off_p_br_  = offset_if_not_ground(pattern, np_,        branch_idx_);
    off_n_br_  = offset_if_not_ground(pattern, nn_,        branch_idx_);
    off_br_p_  = offset_if_not_ground(pattern, branch_idx_, np_);
    off_br_n_  = offset_if_not_ground(pattern, branch_idx_, nn_);
    off_br_br_ = pattern.offset(branch_idx_, branch_idx_);
}

void Inductor::evaluate(const std::vector<double>& /*voltages*/,
                        NumericMatrix& mat, std::vector<double>& rhs) {
    add_if_valid(mat, off_p_br_,  1.0);
    add_if_valid(mat, off_n_br_, -1.0);
    add_if_valid(mat, off_br_p_,  1.0);
    add_if_valid(mat, off_br_n_, -1.0);

    if (transient_) {
        double r_eq, v_eq;

        if (integration_method_ == 1 && gear_ready_) {
            // Gear BDF-2 with variable-timestep coefficients
            // From ngspice NIcomCof: r = dt_prev/dt
            //   ag0 = (2+r)/((1+r)*dt)
            //   ag1 = -(1+r)/(r*dt)
            //   ag2 = 1/((1+r)*r*dt)
            double r = (dt_prev_ > 0.0) ? dt_prev_ / dt_ : 1.0;
            double ag0 = (2.0 + r) / ((1.0 + r) * dt_);
            r_eq = ag0 * inductance_eff_;
            double ag1 = -(1.0 + r) / (r * dt_);
            double ag2 = 1.0 / ((1.0 + r) * r * dt_);
            v_eq = -inductance_eff_ * (ag1 * i_prev_ + ag2 * i_prev2_);
        } else {
            r_eq = 2.0 * inductance_eff_ / dt_;
            v_eq = r_eq * i_prev_ + v_prev_;
        }

        mat.add(off_br_br_, -r_eq);
        if (branch_idx_ >= 0)
            rhs[branch_idx_] -= v_eq;
    }
}

void Inductor::ac_stamp(const std::vector<double>& /*voltages*/,
                        NumericMatrix& G, NumericMatrix& C) {
    // KCL coupling into G: same ±1 as DC
    add_if_valid(G, off_p_br_,  1.0);
    add_if_valid(G, off_n_br_, -1.0);
    // Branch equation coupling into G
    add_if_valid(G, off_br_p_,  1.0);
    add_if_valid(G, off_br_n_, -1.0);
    // Inductance into C: branch equation gets -L at (branch, branch)
    // This represents V(np)-V(nn) = jwL * I_branch in frequency domain
    C.add(off_br_br_, -inductance_eff_);
}

void Inductor::set_transient(double dt) {
    transient_ = true;
    dt_ = dt;
}

void Inductor::clear_transient() {
    transient_ = false;
    dt_ = 0.0;
    gear_ready_ = false;
}

void Inductor::set_integration_method(int method) {
    integration_method_ = method;
}

void Inductor::accept_step(double i_branch, double v_across) {
    // Track flux history for LTE (before i_prev_ is overwritten)
    phi_prev3_ = phi_prev2_;
    phi_prev2_ = phi_prev_;
    phi_prev_ = inductance_eff_ * i_prev_;
    dt_prev_ = dt_;

    v_prev2_ = v_prev_;
    i_prev2_ = i_prev_;
    i_prev_ = i_branch;
    v_prev_ = v_across;
    if (integration_method_ == 1 && !gear_ready_) {
        gear_ready_ = true;
    }
}

void Inductor::accept_step_from_solution(const std::vector<double>& sol) {
    double va = (np_ >= 0) ? sol[np_] : 0.0;
    double vc = (nn_ >= 0) ? sol[nn_] : 0.0;
    double i  = (branch_idx_ >= 0) ? sol[branch_idx_] : 0.0;
    accept_step(i, va - vc);
}

void Inductor::init_dc_state(const std::vector<double>& sol) {
    double va = (np_ >= 0) ? sol[np_] : 0.0;
    double vc = (nn_ >= 0) ? sol[nn_] : 0.0;
    double i  = (branch_idx_ >= 0) ? sol[branch_idx_] : 0.0;
    i_prev_ = i;
    v_prev_ = va - vc;
    i_prev2_ = i;
    v_prev2_ = va - vc;
    gear_ready_ = false;

    // Initialize flux history
    phi_prev_ = inductance_eff_ * i_prev_;
    phi_prev2_ = phi_prev_;
    phi_prev3_ = phi_prev_;
}

void Inductor::init_dc_state_gear(double i_prev, double v_prev,
                                   double i_prev2, double v_prev2) {
    i_prev_ = i_prev;
    v_prev_ = v_prev;
    i_prev2_ = i_prev2;
    v_prev2_ = v_prev2;
    gear_ready_ = true;
}

std::vector<std::string> Inductor::output_currents() const {
    return { "i(" + name_ + ")" };
}

// ---------------------------------------------------------------------------
// compute_trunc — LTE-based timestep control for inductor flux
//
// Mirrors ngspice CKTterr(INDflux, ckt, timeStep) in cktterr.c.
// Uses divided differences of flux history to estimate local truncation
// error, then limits the next timestep so LTE stays within tolerance.
// ---------------------------------------------------------------------------
double Inductor::compute_trunc(const IntegratorCtx& ctx,
                               const SimOptions& opts) const {
    if (!transient_ || ctx.order < 2 || ctx.delta <= 0.0)
        return 1e30;

    const double h0 = ctx.delta;              // current timestep
    const double h1 = ctx.delta_old[1];       // previous timestep
    if (h1 <= 0.0) return 1e30;

    // Current flux
    double phi0 = inductance_eff_ * i_prev_;    // flux at current accepted solution
    double phi1 = phi_prev_;                 // flux at previous step
    double phi2 = phi_prev2_;                // flux at two steps ago

    // Second divided difference of flux (CKTterr divided-difference loop)
    double dd1 = (phi0 - phi1) / h0;
    double dd2 = ((phi0 - phi1) / h0 - (phi1 - phi2) / h1) / (h0 + h1);

    // Tolerance (matches CKTterr):
    //   volttol = abstol + reltol * max(|v_branch0|, |v_branch1|)
    //   chargetol = reltol * max(|phi0|, chgtol) / delta
    //   tol = max(volttol, chargetol)
    // For inductor, the "voltage" on the state variable is the voltage across
    // the inductor (v_prev_), which is the derivative of flux.
    double volttol = opts.abstol + opts.reltol *
                     std::max(std::abs(v_prev_), std::abs(dd1));
    double chargetol = opts.reltol *
                       std::max(std::abs(phi0), opts.chgtol) / h0;
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

void Inductor::process_temperature(double sim_temp, double sim_tnom) {
    // Follows ngspice indtemp.c exactly:
    //   difference = (device_temp + dtemp) - tnom
    //   factor = 1 + tc1*diff + tc2*diff^2
    //   induct = induct * factor * scale
    double tnom = sim_tnom;
    double temp = (temp_ > 0) ? temp_ : sim_temp;
    double dtemp = (temp_ > 0) ? 0.0 : dtemp_;
    double difference = (temp + dtemp) - tnom;
    double factor = 1.0 + tc1_ * difference + tc2_ * difference * difference;
    inductance_eff_ = inductance_nom_ * factor * scale_;
}

} // namespace neospice
