#include "devices/inductor.hpp"
#include <stdexcept>

namespace neospice {

Inductor::Inductor(std::string name, int32_t node_pos, int32_t node_neg, double inductance)
    : Device(std::move(name)), np_(node_pos), nn_(node_neg), inductance_(inductance)
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
            r_eq = 1.5 * inductance_ / dt_;
            v_eq = (inductance_ / (2.0 * dt_)) * (4.0 * i_prev_ - i_prev2_);
        } else {
            r_eq = 2.0 * inductance_ / dt_;
            v_eq = r_eq * i_prev_ + v_prev_;
        }

        mat.add(off_br_br_, -r_eq);
        if (branch_idx_ >= 0)
            rhs[branch_idx_] += v_eq;
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
    C.add(off_br_br_, -inductance_);
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

} // namespace neospice
