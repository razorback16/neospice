#include "devices/coupled_inductor.hpp"
#include <cmath>
#include <stdexcept>

namespace neospice {

CoupledInductor::CoupledInductor(std::string name, Inductor* l1, Inductor* l2, double coupling)
    : Device(std::move(name)), l1_(l1), l2_(l2), coupling_(coupling),
      mutual_([&]{
          if (!l1 || !l2)
              throw std::invalid_argument("CoupledInductor: null inductor pointer");
          if (coupling < -1.0 || coupling > 1.0)
              throw std::invalid_argument("CoupledInductor: coupling coefficient must be in [-1, 1]");
          return coupling * std::sqrt(l1->inductance() * l2->inductance());
      }())
{}

void CoupledInductor::stamp_pattern(SparsityBuilder& builder) const {
    int32_t br1 = l1_->branch_index();
    int32_t br2 = l2_->branch_index();
    if (br1 < 0 || br2 < 0) {
        throw std::logic_error("CoupledInductor::stamp_pattern called before inductors have branch indices");
    }
    builder.add(br1, br2);
    builder.add(br2, br1);
}

void CoupledInductor::assign_offsets(const SparsityPattern& pattern) {
    int32_t br1 = l1_->branch_index();
    int32_t br2 = l2_->branch_index();
    off_br1_br2_ = pattern.offset(br1, br2);
    off_br2_br1_ = pattern.offset(br2, br1);
}

void CoupledInductor::evaluate(const std::vector<double>& /*voltages*/,
                                NumericMatrix& mat, std::vector<double>& rhs) {
    // DC: no stamp needed. Inductors are short circuits at DC,
    // and mutual inductance (M * dI/dt) is zero at DC.
    if (!transient_) return;

    // Transient companion model for mutual coupling.
    // The inductor's own R_eq and V_eq handle the self-inductance.
    // We add the cross-coupling terms for the mutual inductance.

    double r_eq_m;  // mutual companion resistance

    if (integration_method_ == 1 && gear_ready_) {
        // Gear BDF-2 with variable-timestep coefficients
        // From ngspice NIcomCof: r = dt_prev/dt
        //   ag0 = (2+r)/((1+r)*dt)
        //   ag1 = -(1+r)/(r*dt)
        //   ag2 = 1/((1+r)*r*dt)
        double r = (dt_prev_ > 0.0) ? dt_prev_ / dt_ : 1.0;
        double ag0 = (2.0 + r) / ((1.0 + r) * dt_);
        r_eq_m = ag0 * mutual_;
        double ag1 = -(1.0 + r) / (r * dt_);
        double ag2 = 1.0 / ((1.0 + r) * r * dt_);
        double v_eq_12 = -mutual_ * (ag1 * i2_prev_ + ag2 * i2_prev2_);
        double v_eq_21 = -mutual_ * (ag1 * i1_prev_ + ag2 * i1_prev2_);

        add_if_valid(mat, off_br1_br2_, -r_eq_m);
        add_if_valid(mat, off_br2_br1_, -r_eq_m);

        int32_t br1 = l1_->branch_index();
        int32_t br2 = l2_->branch_index();
        add_rhs_if_valid(rhs, br1, -v_eq_12);
        add_rhs_if_valid(rhs, br2, -v_eq_21);
    } else {
        // Trapezoidal: R_eq_m = 2 * M / dt
        r_eq_m = 2.0 * mutual_ / dt_;

        // Trapezoidal companion: V_eq = R_eq_m * I_partner_prev
        double v_eq_12 = r_eq_m * i2_prev_;
        double v_eq_21 = r_eq_m * i1_prev_;

        add_if_valid(mat, off_br1_br2_, -r_eq_m);
        add_if_valid(mat, off_br2_br1_, -r_eq_m);

        int32_t br1 = l1_->branch_index();
        int32_t br2 = l2_->branch_index();
        add_rhs_if_valid(rhs, br1, -v_eq_12);
        add_rhs_if_valid(rhs, br2, -v_eq_21);
    }
}

void CoupledInductor::ac_stamp(const std::vector<double>& /*voltages*/,
                                NumericMatrix& G, NumericMatrix& C) {
    (void)G;
    // Mutual inductance in the C matrix:
    // L1 branch equation: ... - jω*M * I_L2 = 0  → C[br1, br2] += -M
    // L2 branch equation: ... - jω*M * I_L1 = 0  → C[br2, br1] += -M
    add_if_valid(C, off_br1_br2_, -mutual_);
    add_if_valid(C, off_br2_br1_, -mutual_);
}

void CoupledInductor::set_transient(double dt) {
    transient_ = true;
    dt_ = dt;
}

void CoupledInductor::clear_transient() {
    transient_ = false;
    dt_ = 0.0;
    gear_ready_ = false;
}

void CoupledInductor::set_integration_method(int method) {
    integration_method_ = method;
}

void CoupledInductor::init_dc_state(const std::vector<double>& sol) {
    int32_t br1 = l1_->branch_index();
    int32_t br2 = l2_->branch_index();
    i1_prev_ = (br1 >= 0) ? sol[br1] : 0.0;
    i2_prev_ = (br2 >= 0) ? sol[br2] : 0.0;
    i1_prev2_ = i1_prev_;
    i2_prev2_ = i2_prev_;
    gear_ready_ = false;
}

void CoupledInductor::accept_step_from_solution(const std::vector<double>& sol) {
    int32_t br1 = l1_->branch_index();
    int32_t br2 = l2_->branch_index();
    double i1 = (br1 >= 0) ? sol[br1] : 0.0;
    double i2 = (br2 >= 0) ? sol[br2] : 0.0;

    dt_prev_ = dt_;

    // Shift history
    i1_prev2_ = i1_prev_;
    i2_prev2_ = i2_prev_;
    i1_prev_ = i1;
    i2_prev_ = i2;

    if (integration_method_ == 1 && !gear_ready_) {
        gear_ready_ = true;
    }
}

} // namespace neospice
