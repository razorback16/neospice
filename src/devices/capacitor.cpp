#include "devices/capacitor.hpp"

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
        // Gear BDF-2
        g_eq = 1.5 * cap_ / dt_;
        i_eq = (cap_ / (2.0 * dt_)) * (4.0 * v_prev_ - v_prev2_);
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
    if (integration_method_ == 1 && gear_ready_) {
        double i_new = (cap_ / (2.0 * dt_)) * (3.0 * v_across - 4.0 * v_prev_ + v_prev2_);
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
}

void Capacitor::init_dc_state_gear(double v_prev, double i_prev,
                                    double v_prev2, double i_prev2) {
    v_prev_ = v_prev;
    i_prev_ = i_prev;
    v_prev2_ = v_prev2;
    i_prev2_ = i_prev2;
    gear_ready_ = true;
}

} // namespace neospice
