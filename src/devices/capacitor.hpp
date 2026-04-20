#pragma once
#include "devices/device.hpp"

namespace neospice {

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
    void set_integration_method(int method);  // 0=trap, 1=gear2
    void accept_step(double v_across);       // save state after converged step

    // Accept step using full solution vector (device computes v_across internally)
    void accept_step_from_solution(const std::vector<double>& sol);

    // Initialize DC steady-state: sets v_prev and i_prev=0 (no transient current)
    void init_dc_state(const std::vector<double>& sol);

    // Initialize with explicit history for Gear BDF-2 (marks gear_ready)
    void init_dc_state_gear(double v_prev, double i_prev, double v_prev2, double i_prev2);

    // LTE-based timestep control
    double compute_trunc(const IntegratorCtx& ctx,
                         const SimOptions& opts) const override;

    double capacitance() const { return cap_eff_; }
    double capacitance_nom() const { return cap_nom_; }

    /// Temperature coefficient setters (instance-level parameters)
    void set_tc1(double tc1) { tc1_ = tc1; }
    void set_tc2(double tc2) { tc2_ = tc2; }
    void set_scale(double s) { scale_ = s; }
    void set_temp(double t) { temp_ = t; }
    void set_dtemp(double dt) { dtemp_ = dt; }

    /// Apply temperature-dependent adjustment to effective capacitance.
    void process_temperature(double sim_temp, double sim_tnom);

private:
    int32_t np_, nn_;
    double cap_nom_;       // original (nominal) capacitance
    double cap_eff_;       // effective value after temperature/scale adjustment

    double tc1_ = 0.0;
    double tc2_ = 0.0;
    double scale_ = 1.0;
    double temp_ = -1.0;  // device temperature in K (-1 = use simulation default)
    double dtemp_ = 0.0;  // delta temperature in K
    bool transient_ = false;
    double dt_ = 0.0;
    double v_prev_ = 0.0;
    double i_prev_ = 0.0;
    int integration_method_ = 0;  // 0=trapezoidal, 1=gear2
    bool gear_ready_ = false;
    double v_prev2_ = 0.0;        // two-step-back voltage for Gear
    double i_prev2_ = 0.0;        // two-step-back current for Gear

    // Charge history for LTE-based timestep control (compute_trunc)
    double q_prev_ = 0.0;         // Q(n-1) = C * v_prev
    double q_prev2_ = 0.0;        // Q(n-2)
    double q_prev3_ = 0.0;        // Q(n-3)
    double dt_prev_ = 0.0;        // timestep at previous accepted step

    MatrixOffset off_pp_ = -1, off_pn_ = -1, off_np_ = -1, off_nn_ = -1;
};

} // namespace neospice
