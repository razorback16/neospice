#include "devices/inductor.hpp"
#include <stdexcept>

namespace cudaspice {

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
    // KCL rows: np gets +I_branch, nn gets -I_branch
    add_if_valid(mat, off_p_br_,  1.0);
    add_if_valid(mat, off_n_br_, -1.0);

    // Branch equation coefficients
    add_if_valid(mat, off_br_p_,  1.0);
    add_if_valid(mat, off_br_n_, -1.0);

    if (transient_) {
        // Trapezoidal companion model:
        //   R_eq = 2L / dt
        //   V_eq = R_eq * I_prev + V_prev
        //   Branch equation: V(np) - V(nn) - R_eq * I_branch = V_eq
        const double r_eq = 2.0 * inductance_ / dt_;
        const double v_eq = r_eq * i_prev_ + v_prev_;

        // -R_eq at (branch, branch)
        mat.add(off_br_br_, -r_eq);

        // RHS[branch] = V_eq
        if (branch_idx_ >= 0)
            rhs[branch_idx_] += v_eq;
    }
    // DC: R_eq = 0, V_eq = 0 => branch equation is just V(np) - V(nn) = 0
    // off_br_br_ gets no contribution and rhs[branch] += 0
}

void Inductor::ac_stamp(const std::vector<double>& /*voltages*/,
                        NumericMatrix& /*G*/, NumericMatrix& /*C*/) {
    // AC: same coupling stamps as DC.
    // The jwL impedance is handled by the AC solver using inductance().
    // We stamp the KCL and branch equation coupling into G.
    // (This is typically done by the AC solver framework, not here.)
}

void Inductor::set_transient(double dt) {
    transient_ = true;
    dt_ = dt;
}

void Inductor::clear_transient() {
    transient_ = false;
    dt_ = 0.0;
}

void Inductor::accept_step(double i_branch, double v_across) {
    i_prev_ = i_branch;
    v_prev_ = v_across;
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
}

std::vector<std::string> Inductor::output_currents() const {
    return { "i(" + name_ + ")" };
}

} // namespace cudaspice
