#pragma once
#include "devices/device.hpp"

namespace neospice {

class Inductor : public Device {
public:
    Inductor(std::string name, int32_t node_pos, int32_t node_neg, double inductance);

    void set_branch_index(int32_t idx);
    int32_t branch_index() const { return branch_idx_; }

    int32_t extra_vars() const override { return 1; }
    void assign_branch_index(int32_t& next) override {
        set_branch_index(next); next += extra_vars();
    }

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

    void set_transient(double dt);
    void clear_transient();
    void set_integration_method(int method);  // 0=trap, 1=gear2
    void accept_step(double i_branch, double v_across);
    void accept_step_from_solution(const std::vector<double>& sol);
    void init_dc_state(const std::vector<double>& sol);
    void init_dc_state_gear(double i_prev, double v_prev,
                            double i_prev2, double v_prev2);

    double inductance() const { return inductance_; }

    std::vector<std::string> output_currents() const override;

private:
    int32_t np_, nn_;
    int32_t branch_idx_ = -1;
    double inductance_;
    bool transient_ = false;
    double dt_ = 0.0;
    double v_prev_ = 0.0;
    double i_prev_ = 0.0;
    int integration_method_ = 0;  // 0=trapezoidal, 1=gear2
    double v_prev2_ = 0.0;
    double i_prev2_ = 0.0;
    bool gear_ready_ = false;

    MatrixOffset off_p_br_  = -1;  // (np, branch)
    MatrixOffset off_n_br_  = -1;  // (nn, branch)
    MatrixOffset off_br_p_  = -1;  // (branch, np)
    MatrixOffset off_br_n_  = -1;  // (branch, nn)
    MatrixOffset off_br_br_ = -1;  // (branch, branch)
};

} // namespace neospice
