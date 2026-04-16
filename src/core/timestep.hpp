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

private:
    double dt_ = 0.0;
    double time_ = 0.0;
    double tstop_ = 0.0;
    double proposed_dt_ = 0.0;
    int rejected_ = 0;
    std::set<double> breakpoints_;
};

} // namespace neospice
