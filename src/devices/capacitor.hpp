#pragma once
#include "devices/device.hpp"

namespace cudaspice {

class Capacitor : public Device {
public:
    Capacitor(std::string name, int32_t node_pos, int32_t node_neg, double capacitance);

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

    void set_transient(double dt);           // enable transient companion model
    void clear_transient();                  // disable (back to DC open circuit)
    void accept_step(double v_across);       // save state after converged step

    // Accept step using full solution vector (device computes v_across internally)
    void accept_step_from_solution(const std::vector<double>& sol);

    // Initialize DC steady-state: sets v_prev and i_prev=0 (no transient current)
    void init_dc_state(const std::vector<double>& sol);

private:
    int32_t np_, nn_;
    double cap_;
    bool transient_ = false;
    double dt_ = 0.0;
    double v_prev_ = 0.0;
    double i_prev_ = 0.0;

    MatrixOffset off_pp_ = -1, off_pn_ = -1, off_np_ = -1, off_nn_ = -1;
};

} // namespace cudaspice
