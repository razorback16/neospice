#include "devices/bsim4v7/bsim4v7.hpp"
#include <algorithm>
#include <cmath>

namespace neospice {

namespace {

// DEVfetlim-style Vgs clamp (ngspice mosfet.c:mos_limit / DEVfetlim).
// Limits |Vgs_new - Vgs_old| to max(0.5, 0.5*|Vgs_old - Vth|).
double fetlim_vgs(double vgs_new, double vgs_old, double vth) {
    double delta = vgs_new - vgs_old;
    double limit = std::max(0.5, 0.5 * std::abs(vgs_old - vth));
    if (std::abs(delta) > limit) {
        return vgs_old + (delta > 0 ? limit : -limit);
    }
    return vgs_new;
}

// DEVlimvds-style Vds clamp. Limits |Vds_new - Vds_old| to 0.5V.
double fetlim_vds(double vds_new, double vds_old) {
    double delta = vds_new - vds_old;
    if (std::abs(delta) > 0.5) {
        return vds_old + (delta > 0 ? 0.5 : -0.5);
    }
    return vds_new;
}

} // namespace

BSIM4v7::BSIM4v7(std::string name, int32_t node_drain, int32_t node_gate,
                  int32_t node_source, int32_t node_body,
                  const BSIM4v7Params& params)
    : Device(std::move(name)), nd_(node_drain), ng_(node_gate),
      ns_(node_source), nb_(node_body), params_(params) {
    for (auto& row : off_) for (auto& o : row) o = -1;
}

int32_t BSIM4v7::terminal(int i) const {
    switch (i) {
        case 0: return nd_;
        case 1: return ng_;
        case 2: return ns_;
        case 3: return nb_;
        default: return -1;
    }
}

void BSIM4v7::stamp_pattern(SparsityBuilder& builder) const {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            stamp_if_not_ground(builder, terminal(i), terminal(j));
        }
    }
}

void BSIM4v7::assign_offsets(const SparsityPattern& pattern) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            off_[i][j] = offset_if_not_ground(pattern, terminal(i), terminal(j));
        }
    }
}

void BSIM4v7::evaluate(const std::vector<double>& voltages,
                        NumericMatrix& mat, std::vector<double>& rhs) {
    double vd = (nd_ >= 0) ? voltages[nd_] : 0.0;
    double vg = (ng_ >= 0) ? voltages[ng_] : 0.0;
    double vs = (ns_ >= 0) ? voltages[ns_] : 0.0;
    double vb = (nb_ >= 0) ? voltages[nb_] : 0.0;

    double Vgs = vg - vs;
    double Vds = vd - vs;
    double Vbs = vb - vs;

    // PMOS: invert polarities and use |VTH0| so the internal model runs
    // in "NMOS-like" positive coordinates (ngspice BSIM4 convention).
    double sign = 1.0;
    BSIM4v7Params params_use = params_;
    if (params_.is_pmos) {
        Vgs = -Vgs; Vds = -Vds; Vbs = -Vbs;
        sign = -1.0;
        params_use.VTH0 = std::abs(params_.VTH0);
    }

    auto ev = bsim4v7_evaluate(Vgs, Vds, Vbs, params_use, 300.15);
    last_eval_ = ev;

    double Ids = sign * ev.Ids;
    double gm = ev.gm;
    double gds = ev.gds;
    double gmb = ev.gmb;

    // Norton equivalent: I = gm*Vgs + gds*Vds + gmb*Vbs
    // The linearized current INTO the drain is:
    //   Id_lin = gm*(Vg-Vs) + gds*(Vd-Vs) + gmb*(Vb-Vs)
    // Norton residual: Ids - (gm*Vgs + gds*Vds + gmb*Vbs)
    double Ieq = Ids - (gm * (vg - vs) + gds * (vd - vs) + gmb * (vb - vs));

    // Stamp conductance matrix
    // d row: +gds at (d,d), +gm at (d,g), -(gds+gm+gmb) at (d,s), +gmb at (d,b)
    add_if_valid(mat, off_[0][0],  gds);            // dd
    add_if_valid(mat, off_[0][1],  gm);             // dg
    add_if_valid(mat, off_[0][2], -(gds + gm + gmb)); // ds
    add_if_valid(mat, off_[0][3],  gmb);            // db

    // s row: -(gds) at (s,d), -(gm) at (s,g), +(gds+gm+gmb) at (s,s), -(gmb) at (s,b)
    add_if_valid(mat, off_[2][0], -gds);
    add_if_valid(mat, off_[2][1], -gm);
    add_if_valid(mat, off_[2][2],  gds + gm + gmb);
    add_if_valid(mat, off_[2][3], -gmb);

    // g row: gate current = 0 (ideal MOSFET), no stamps
    // b row: body current = 0 (simplified), no stamps

    // RHS: Norton equivalent current (current OUT of drain = +Ids; move to RHS flips sign)
    add_rhs_if_valid(rhs, nd_, -Ieq);
    add_rhs_if_valid(rhs, ns_,  Ieq);
}

void BSIM4v7::limit_voltages(const std::vector<double>& old_v,
                              std::vector<double>& new_v) {
    auto node_v = [](const std::vector<double>& v, int32_t n) {
        return (n >= 0) ? v[n] : 0.0;  // ground if -1
    };

    // ngspice-style DEVfetlim/DEVlimvds Newton step limiting. Clamp:
    //   |Vgs_new - Vgs_old| <= max(0.5, 0.5*|Vgs_old - Vth|)
    //   |Vds_new - Vds_old| <= 0.5
    // This breaks the two-state limit cycles diagnosed in M3 where Newton
    // oscillated between overshoot states (see memory m3-t3-t4-diagnosis).
    double vg_old = node_v(old_v, ng_);
    double vs_old = node_v(old_v, ns_);
    double vd_old = node_v(old_v, nd_);
    double vg_new = node_v(new_v, ng_);
    double vs_new = node_v(new_v, ns_);
    double vd_new = node_v(new_v, nd_);

    double vgs_old = vg_old - vs_old;
    double vds_old = vd_old - vs_old;
    double vgs_new = vg_new - vs_new;
    double vds_new = vd_new - vs_new;

    // Approximate Vth by VTH0. Exact Vth depends on Vbs, but for the
    // purposes of Newton step limiting VTH0 is sufficient (ngspice does
    // the same in DEVfetlim for zero-body-bias behavior).
    double vth = params_.VTH0;
    double vgs_clamped = fetlim_vgs(vgs_new, vgs_old, vth);
    double vds_clamped = fetlim_vds(vds_new, vds_old);

    // Redistribute clamped Vgs/Vds back to absolute node voltages. Pin the
    // (possibly moved) new source voltage so we don't over/under-compensate
    // if the source moved between iterations.
    if (ng_ >= 0) new_v[ng_] = vs_new + vgs_clamped;
    if (nd_ >= 0) new_v[nd_] = vs_new + vds_clamped;
}

void BSIM4v7::ac_stamp(const std::vector<double>& voltages,
                        NumericMatrix& G, NumericMatrix& C) {
    // G: same conductance stamps as evaluate (at DC OP)
    auto& ev = last_eval_;
    double gm = ev.gm, gds = ev.gds, gmb = ev.gmb;

    add_if_valid(G, off_[0][0],  gds);
    add_if_valid(G, off_[0][1],  gm);
    add_if_valid(G, off_[0][2], -(gds + gm + gmb));
    add_if_valid(G, off_[0][3],  gmb);
    add_if_valid(G, off_[2][0], -gds);
    add_if_valid(G, off_[2][1], -gm);
    add_if_valid(G, off_[2][2],  gds + gm + gmb);
    add_if_valid(G, off_[2][3], -gmb);

    // C: capacitance stamps
    // Cgs between gate and source
    add_if_valid(C, off_[1][1],  ev.Cgs + ev.Cgd + ev.Cgb); // gg
    add_if_valid(C, off_[1][2], -ev.Cgs);                     // gs
    add_if_valid(C, off_[1][0], -ev.Cgd);                     // gd
    add_if_valid(C, off_[1][3], -ev.Cgb);                     // gb

    add_if_valid(C, off_[2][1], -ev.Cgs);                     // sg
    add_if_valid(C, off_[2][2],  ev.Cgs + ev.Cbs);           // ss
    add_if_valid(C, off_[2][3], -ev.Cbs);                     // sb

    add_if_valid(C, off_[0][1], -ev.Cgd);                     // dg
    add_if_valid(C, off_[0][0],  ev.Cgd + ev.Cbd);           // dd
    add_if_valid(C, off_[0][3], -ev.Cbd);                     // db

    add_if_valid(C, off_[3][1], -ev.Cgb);                     // bg
    add_if_valid(C, off_[3][2], -ev.Cbs);                     // bs
    add_if_valid(C, off_[3][0], -ev.Cbd);                     // bd
    add_if_valid(C, off_[3][3],  ev.Cgb + ev.Cbs + ev.Cbd);  // bb
}

// ---------------------------------------------------------------------------
// Transient companion model for intrinsic MOSFET charges (Qg, Qd, Qb)
// ---------------------------------------------------------------------------

void BSIM4v7::set_transient(double dt) {
    transient_ = true;
    dt_ = dt;
}

void BSIM4v7::clear_transient() {
    transient_ = false;
    dt_ = 0.0;
}

void BSIM4v7::set_integration_method(int method) {
    integration_method_ = method;
}

void BSIM4v7::init_dc_state(const std::vector<double>& sol) {
    double vd = (nd_ >= 0) ? sol[nd_] : 0.0;
    double vg = (ng_ >= 0) ? sol[ng_] : 0.0;
    double vs = (ns_ >= 0) ? sol[ns_] : 0.0;
    double vb = (nb_ >= 0) ? sol[nb_] : 0.0;

    double Vgs = vg - vs, Vds = vd - vs, Vbs = vb - vs;
    BSIM4v7Params params_use = params_;
    if (params_.is_pmos) {
        Vgs = -Vgs; Vds = -Vds; Vbs = -Vbs;
        params_use.VTH0 = std::abs(params_.VTH0);
    }

    auto ev = bsim4v7_evaluate(Vgs, Vds, Vbs, params_use, 300.15);
    Qg_prev_ = ev.Qg; Qd_prev_ = ev.Qd; Qb_prev_ = ev.Qb;
    Ig_prev_ = 0.0; Id_cap_prev_ = 0.0; Ib_prev_ = 0.0;
}

void BSIM4v7::accept_step_from_solution(const std::vector<double>& sol) {
    if (!transient_ || dt_ <= 0.0) return;

    double vd = (nd_ >= 0) ? sol[nd_] : 0.0;
    double vg = (ng_ >= 0) ? sol[ng_] : 0.0;
    double vs = (ns_ >= 0) ? sol[ns_] : 0.0;
    double vb = (nb_ >= 0) ? sol[nb_] : 0.0;

    double Vgs = vg - vs, Vds = vd - vs, Vbs = vb - vs;
    BSIM4v7Params params_use = params_;
    if (params_.is_pmos) {
        Vgs = -Vgs; Vds = -Vds; Vbs = -Vbs;
        params_use.VTH0 = std::abs(params_.VTH0);
    }

    auto ev = bsim4v7_evaluate(Vgs, Vds, Vbs, params_use, 300.15);

    // Trapezoidal: I = 2*(Q_new - Q_prev)/dt - I_prev
    double Ig_new = 2.0 * (ev.Qg - Qg_prev_) / dt_ - Ig_prev_;
    double Id_new = 2.0 * (ev.Qd - Qd_prev_) / dt_ - Id_cap_prev_;
    double Ib_new = 2.0 * (ev.Qb - Qb_prev_) / dt_ - Ib_prev_;

    Qg_prev_ = ev.Qg; Qd_prev_ = ev.Qd; Qb_prev_ = ev.Qb;
    Ig_prev_ = Ig_new; Id_cap_prev_ = Id_new; Ib_prev_ = Ib_new;
}

} // namespace neospice
