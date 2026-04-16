#include "devices/bsim4v7/bsim4v7.hpp"
#include <cmath>

namespace neospice {

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

    // PMOS: invert polarities
    double sign = 1.0;
    if (params_.is_pmos) {
        Vgs = -Vgs; Vds = -Vds; Vbs = -Vbs;
        sign = -1.0;
    }

    auto ev = bsim4v7_evaluate(Vgs, Vds, Vbs, params_, 300.15);
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

    // RHS: Norton equivalent current
    add_rhs_if_valid(rhs, nd_,  Ieq);   // current into drain
    add_rhs_if_valid(rhs, ns_, -Ieq);   // current out of source
}

void BSIM4v7::limit_voltages(const std::vector<double>& old_v,
                              std::vector<double>& new_v) {
    // Gate voltage limiting
    if (ng_ >= 0 && ns_ >= 0) {
        double vgs_old = old_v[ng_] - old_v[ns_];
        double vgs_new = new_v[ng_] - new_v[ns_];
        double max_step = 0.5;  // limit Vgs change per iteration
        if (std::abs(vgs_new - vgs_old) > max_step) {
            double delta = (vgs_new > vgs_old) ? max_step : -max_step;
            new_v[ng_] = old_v[ng_] + delta;
        }
    }
    // Drain voltage limiting
    if (nd_ >= 0 && ns_ >= 0) {
        double vds_old = old_v[nd_] - old_v[ns_];
        double vds_new = new_v[nd_] - new_v[ns_];
        double max_step = 0.5;
        if (std::abs(vds_new - vds_old) > max_step) {
            double delta = (vds_new > vds_old) ? max_step : -max_step;
            new_v[nd_] = old_v[nd_] + delta;
        }
    }
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

} // namespace neospice
