#pragma once
#include "core/types.hpp"
#include <vector>
#include <set>

namespace neospice {

class TimeStepController {
public:
    void init(double initial_dt, double tstop);

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
    double clamp_to_breakpoint(double proposed_dt) const;
    double clamp_to_end(double proposed_dt) const;

    int rejected_count() const { return rejected_; }
    void record_rejection() { ++rejected_; }

    /// Integration order: 1 = Backward Euler, 2 = Gear-2.
    /// Starts at 1; caller increments once a second accepted step is available.
    int order() const { return order_; }
    void set_order(int o) { order_ = o; }

    /// Previous accepted step size (h_{n-1}), set by the transient driver.
    double prev_dt() const { return prev_dt_; }
    void set_prev_dt(double h) { prev_dt_ = h; }

private:
    double dt_ = 0.0;
    double time_ = 0.0;
    double tstop_ = 0.0;
    double proposed_dt_ = 0.0;
    int rejected_ = 0;
    int order_ = 1;
    double prev_dt_ = 0.0;
    std::set<double> breakpoints_;
};

} // namespace neospice
