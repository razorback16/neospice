#include "core/timestep.hpp"
#include <cmath>
#include <algorithm>

namespace neospice {

void TimeStepController::init(double initial_dt, double tstop) {
    dt_ = initial_dt;
    tstop_ = tstop;
    time_ = 0.0;
    proposed_dt_ = initial_dt;
    rejected_ = 0;
    breakpoints_.clear();
    source_breakpoints_.clear();
}

void TimeStepController::advance(double dt) {
    time_ += dt;
    crossed_bp_ = false;
    crossed_src_bp_ = false;
    while (!breakpoints_.empty() && *breakpoints_.begin() <= time_ + 1e-18) {
        breakpoints_.erase(breakpoints_.begin());
        crossed_bp_ = true;
    }
    while (!source_breakpoints_.empty() && *source_breakpoints_.begin() <= time_ + 1e-18) {
        source_breakpoints_.erase(source_breakpoints_.begin());
        crossed_src_bp_ = true;
    }
}

bool TimeStepController::evaluate_step(const std::vector<double>& sol,
                                        const std::vector<double>& sol_prev,
                                        const std::vector<double>& sol_prev2,
                                        int32_t num_nodes,
                                        const SimOptions& opts) {
    double max_ratio = 0.0;
    for (int32_t i = 0; i < num_nodes; ++i) {
        double delta2 = sol[i] - 2.0 * sol_prev[i] + sol_prev2[i];
        double lte = std::abs(delta2) / 12.0;
        double tol = opts.reltol * std::abs(sol[i]) + opts.vntol;
        if (tol > 0.0) {
            max_ratio = std::max(max_ratio, lte / tol);
        }
    }

    if (max_ratio <= opts.trtol) {
        double safety = 0.8;
        double growth = 2.0;
        if (max_ratio > 1e-10) {
            double factor = safety * std::pow(opts.trtol / max_ratio, 1.0 / 3.0);
            factor = std::min(factor, growth);
            factor = std::max(factor, 0.5);
            proposed_dt_ = dt_ * factor;
        } else {
            proposed_dt_ = dt_ * growth;
        }
        return true;
    } else {
        rejected_++;
        double safety = 0.8;
        double shrink = 0.25;
        double factor = safety * std::pow(opts.trtol / max_ratio, 1.0 / 3.0);
        factor = std::max(factor, shrink);
        proposed_dt_ = dt_ * factor;
        return false;
    }
}

void TimeStepController::add_breakpoint(double t) {
    if (t > time_ && t <= tstop_) {
        breakpoints_.insert(t);
    }
}

void TimeStepController::add_source_breakpoint(double t) {
    if (t > time_ && t <= tstop_) {
        breakpoints_.insert(t);
        source_breakpoints_.insert(t);
    }
}

double TimeStepController::clamp_to_breakpoint(double proposed_dt) const {
    double t_next = time_ + proposed_dt;
    if (!breakpoints_.empty()) {
        double bp = *breakpoints_.begin();
        if (t_next > bp - 1e-18) {
            return bp - time_;
        }
        if (t_next > bp - 0.1 * proposed_dt) {
            return bp - time_;
        }
    }
    return proposed_dt;
}

double TimeStepController::clamp_to_end(double proposed_dt) const {
    double t_next = time_ + proposed_dt;
    if (t_next > tstop_) {
        return tstop_ - time_;
    }
    return proposed_dt;
}

} // namespace neospice
