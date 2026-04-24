#pragma once
#include "devices/device.hpp"
#include <cmath>
#include <limits>

namespace neospice {

class Inductor : public Device {
public:
    Inductor(std::string name, int32_t node_pos, int32_t node_neg, double inductance);

    void set_branch_index(int32_t idx);
    int32_t branch_index() const override { return branch_idx_; }

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

    double inductance() const { return inductance_eff_; }
    double inductance_nom() const { return inductance_nom_; }

    /// Temperature coefficient setters (instance-level parameters)
    void set_tc1(double tc1) { tc1_ = tc1; }
    void set_tc2(double tc2) { tc2_ = tc2; }
    void set_scale(double s) { scale_ = s; }
    void set_multiplier(double m) { m_ = m; }
    void set_temp(double t) { temp_ = t; }
    void set_dtemp(double dt) { dtemp_ = dt; }

    /// Initial condition (IC=) support
    void set_ic(double i) { ic_ = i; }
    bool has_ic() const { return !std::isnan(ic_); }
    double ic() const { return ic_; }
    void apply_ic_override(std::vector<double>& sol);  // Override i_prev/solution with IC value

    /// Apply temperature-dependent adjustment to effective inductance.
    void process_temperature(double sim_temp, double sim_tnom) override;

    std::vector<std::string> output_currents() const override;

    // LTE-based timestep control
    double compute_trunc(const IntegratorCtx& ctx,
                         const SimOptions& opts) const override;

private:
    int32_t np_, nn_;
    int32_t branch_idx_ = -1;
    double inductance_nom_;    // original (nominal) inductance
    double inductance_eff_;    // effective value after temperature/scale adjustment

    double tc1_ = 0.0;
    double tc2_ = 0.0;
    double scale_ = 1.0;
    double m_ = 1.0;          // multiplier (m instances in parallel)
    double temp_ = -1.0;      // device temperature in K (-1 = use simulation default)
    double dtemp_ = 0.0;      // delta temperature in K
    double ic_ = std::numeric_limits<double>::quiet_NaN();  // IC= initial current (NaN = not specified)
    bool transient_ = false;
    double dt_ = 0.0;
    double v_prev_ = 0.0;
    double i_prev_ = 0.0;
    int integration_method_ = 0;  // 0=trapezoidal, 1=gear2
    double v_prev2_ = 0.0;
    double i_prev2_ = 0.0;
    bool gear_ready_ = false;

    // Flux history for LTE-based timestep control (compute_trunc)
    double phi_prev_ = 0.0;       // flux(n-1) = L * i_prev
    double phi_prev2_ = 0.0;      // flux(n-2)
    double phi_prev3_ = 0.0;      // flux(n-3)
    double dt_prev_ = 0.0;        // timestep at previous accepted step

    MatrixOffset off_p_br_  = -1;  // (np, branch)
    MatrixOffset off_n_br_  = -1;  // (nn, branch)
    MatrixOffset off_br_p_  = -1;  // (branch, np)
    MatrixOffset off_br_n_  = -1;  // (branch, nn)
    MatrixOffset off_br_br_ = -1;  // (branch, branch)
};

} // namespace neospice
