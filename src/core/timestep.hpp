#pragma once
#include "core/types.hpp"
#include <vector>
#include <set>
#include <map>

namespace neospice {

class TimeStepController {
public:
    /// Breakpoint classification: HARD edges (PULSE, EXP, PWL) get full dt
    /// reduction; SOFT crossings (SIN, AM, SFFM) use a milder scale.
    enum class BreakpointType { HARD, SOFT };

    void init(double initial_dt, double tstop, double max_step = 0.0);

    double current_dt() const { return dt_; }
    double current_time() const { return time_; }
    double proposed_dt() const { return proposed_dt_; }

    void set_dt(double dt) { dt_ = dt; }
    void advance(double dt);

    bool evaluate_step(const std::vector<double>& sol,
                       const std::vector<double>& sol_prev,
                       const std::vector<double>& sol_prev2,
                       int32_t num_nodes,
                       const SimOptions& opts);

    void add_breakpoint(double t);
    void add_source_breakpoint(double t, BreakpointType type = BreakpointType::HARD);
    double clamp_to_breakpoint(double proposed_dt) const;
    double clamp_to_end(double proposed_dt) const;

    /// True if the last advance() consumed one or more SOURCE breakpoints.
    bool crossed_source_breakpoint() const { return crossed_src_bp_; }

    /// Type of the last consumed source breakpoint (HARD if any consumed bp was HARD).
    BreakpointType last_bp_type() const { return last_bp_type_; }

    /// Distance from current_time to the next source breakpoint (or tstop if none).
    double next_breakpoint_gap() const;

    int rejected_count() const { return rejected_; }
    void record_rejection() { ++rejected_; }

    /// Integration order: 1 = Backward Euler, 2 = Trapezoidal or Gear-2.
    /// Starts at 1; caller increments once a second accepted step is available.
    int order() const { return order_; }
    void set_order(int o) { order_ = o; }

    /// Previous accepted step size (h_{n-1}), set by the transient driver.
    double prev_dt() const { return prev_dt_; }
    void set_prev_dt(double h) { prev_dt_ = h; }

    // Ringing detection — detects sign-alternating oscillations in the
    // second differences of node voltages, which indicate trapezoidal
    // integration ringing near sharp transitions.
    void check_ringing(const std::vector<double>& sol,
                       const std::vector<double>& sol_prev,
                       const std::vector<double>& sol_prev2,
                       const std::vector<double>& sol_prev3,
                       int32_t num_nodes,
                       const SimOptions& opts);
    bool ringing_detected() const { return ringing_detected_; }
    int ringing_cooldown() const { return ringing_cooldown_; }
    void tick_cooldown();  // called each accepted step

private:
    double dt_ = 0.0;
    double time_ = 0.0;
    double tstop_ = 0.0;
    double proposed_dt_ = 0.0;
    int rejected_ = 0;
    int order_ = 1;
    double prev_dt_ = 0.0;
    bool crossed_src_bp_ = false;
    BreakpointType last_bp_type_ = BreakpointType::HARD;
    bool ringing_detected_ = false;
    int ringing_cooldown_ = 0;
    std::set<double> breakpoints_;
    std::map<double, BreakpointType> source_breakpoints_;
    double min_break_ = 0.0;  // CKTminBreak: minimum breakpoint spacing (5e-5 * max_step)
    std::vector<double> max_seen_;  // per-node max |value| for lte_ref_mode==2
};

} // namespace neospice
