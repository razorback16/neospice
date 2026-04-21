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
    max_seen_.clear();
}

void TimeStepController::advance(double dt) {
    time_ += dt;
    crossed_src_bp_ = false;
    while (!breakpoints_.empty() && *breakpoints_.begin() <= time_ + 1e-18) {
        breakpoints_.erase(breakpoints_.begin());
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
                                        int32_t num_vars,
                                        const SimOptions& opts) {
    int32_t check_vars = opts.mask_ivars ? num_nodes : num_vars;

    // Pre-compute reference values based on LTE reference mode
    double max_abs = 0.0;
    if (opts.lte_ref_mode == 1) {
        for (int32_t i = 0; i < check_vars; ++i) {
            max_abs = std::max(max_abs, std::abs(sol[i]));
        }
    } else if (opts.lte_ref_mode == 2) {
        if (static_cast<int32_t>(max_seen_.size()) < check_vars) {
            max_seen_.resize(check_vars, 0.0);
        }
        for (int32_t i = 0; i < check_vars; ++i) {
            max_seen_[i] = std::max(max_seen_[i], std::abs(sol[i]));
        }
    }

    double max_ratio = 0.0;
    for (int32_t i = 0; i < check_vars; ++i) {
        double delta2 = sol[i] - 2.0 * sol_prev[i] + sol_prev2[i];
        double lte_coeff = (opts.method == "gear") ? (2.0 / 9.0) : (1.0 / 12.0);
        double lte = std::abs(delta2) * lte_coeff;
        double abs_tol = (i < num_nodes) ? opts.vntol : opts.abstol;
        double tol;
        switch (opts.lte_ref_mode) {
        case 1:
            tol = opts.reltol * max_abs + abs_tol;
            break;
        case 2:
            tol = opts.reltol * max_seen_[i] + abs_tol;
            break;
        default:
            tol = opts.reltol * std::abs(sol[i]) + abs_tol;
            break;
        }
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

double TimeStepController::next_breakpoint_gap() const {
    // Return the gap to the next source breakpoint from current time.
    // If no source breakpoints remain, use the nearest regular breakpoint.
    // If neither, return tstop - time.
    if (!source_breakpoints_.empty()) {
        double bp = *source_breakpoints_.begin();
        if (bp > time_ + 1e-18) {
            return bp - time_;
        }
    }
    if (!breakpoints_.empty()) {
        double bp = *breakpoints_.begin();
        if (bp > time_ + 1e-18) {
            return bp - time_;
        }
    }
    return tstop_ - time_;
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
