#pragma once
#include "devices/device.hpp"

namespace cudaspice {

class Inductor : public Device {
public:
    Inductor(std::string name, int32_t node_pos, int32_t node_neg, double inductance);

    void set_branch_index(int32_t idx);
    int32_t branch_index() const { return branch_idx_; }

    int32_t extra_vars() const override { return 1; }

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

    void set_transient(double dt);
    void clear_transient();
    void accept_step(double i_branch, double v_across);
    void accept_step_from_solution(const std::vector<double>& sol);

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

    MatrixOffset off_p_br_  = -1;  // (np, branch)
    MatrixOffset off_n_br_  = -1;  // (nn, branch)
    MatrixOffset off_br_p_  = -1;  // (branch, np)
    MatrixOffset off_br_n_  = -1;  // (branch, nn)
    MatrixOffset off_br_br_ = -1;  // (branch, branch)
};

} // namespace cudaspice
