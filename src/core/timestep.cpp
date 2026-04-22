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
    ringing_detected_ = false;
    ringing_cooldown_ = 0;
    breakpoints_.clear();
    source_breakpoints_.clear();
    max_seen_.clear();
}

void TimeStepController::advance(double dt) {
    time_ += dt;
    crossed_src_bp_ = false;
    last_bp_type_ = BreakpointType::SOFT;  // reset to mildest; promote to HARD if any consumed bp is HARD
    while (!breakpoints_.empty() && *breakpoints_.begin() <= time_ + 1e-18) {
        breakpoints_.erase(breakpoints_.begin());
    }
    while (!source_breakpoints_.empty() && source_breakpoints_.begin()->first <= time_ + 1e-18) {
        if (source_breakpoints_.begin()->second == BreakpointType::HARD) {
            last_bp_type_ = BreakpointType::HARD;
        }
        source_breakpoints_.erase(source_breakpoints_.begin());
        crossed_src_bp_ = true;
    }
}

bool TimeStepController::evaluate_step(const std::vector<double>& sol,
                                        const std::vector<double>& sol_prev,
                                        const std::vector<double>& sol_prev2,
                                        int32_t num_nodes,
                                        const std::vector<int32_t>& check_indices,
                                        const SimOptions& opts) {
    // Pre-compute reference values based on LTE reference mode
    double max_abs = 0.0;
    if (opts.lte_ref_mode == 1) {
        for (int32_t idx : check_indices) {
            max_abs = std::max(max_abs, std::abs(sol[idx]));
        }
    } else if (opts.lte_ref_mode == 2) {
        int32_t max_idx = 0;
        for (int32_t idx : check_indices) max_idx = std::max(max_idx, idx + 1);
        if (static_cast<int32_t>(max_seen_.size()) < max_idx) {
            max_seen_.resize(max_idx, 0.0);
        }
        for (int32_t idx : check_indices) {
            max_seen_[idx] = std::max(max_seen_[idx], std::abs(sol[idx]));
        }
    }

    double max_ratio = 0.0;
    for (int32_t i : check_indices) {
        double delta2 = sol[i] - 2.0 * sol_prev[i] + sol_prev2[i];
        double lte_coeff = (opts.method == "gear") ? (2.0 / 9.0) : (1.0 / 12.0);
        double lte = std::abs(delta2) * lte_coeff;
        double abs_tol = (i < num_nodes) ? opts.vntol : opts.itol;
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

void TimeStepController::add_source_breakpoint(double t, BreakpointType type) {
    if (t > time_ && t <= tstop_) {
        breakpoints_.insert(t);
        // If a breakpoint already exists at this time, promote to HARD if needed
        auto it = source_breakpoints_.find(t);
        if (it != source_breakpoints_.end()) {
            if (type == BreakpointType::HARD) {
                it->second = BreakpointType::HARD;
            }
        } else {
            source_breakpoints_.emplace(t, type);
        }
    }
}

double TimeStepController::next_breakpoint_gap() const {
    // Return the gap to the next source breakpoint from current time.
    // If no source breakpoints remain, use the nearest regular breakpoint.
    // If neither, return tstop - time.
    if (!source_breakpoints_.empty()) {
        double bp = source_breakpoints_.begin()->first;
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

void TimeStepController::check_ringing(
        const std::vector<double>& sol,
        const std::vector<double>& sol_prev,
        const std::vector<double>& sol_prev2,
        const std::vector<double>& sol_prev3,
        int32_t num_nodes,
        const SimOptions& opts) {
    ringing_detected_ = false;
    for (int32_t i = 0; i < num_nodes; ++i) {
        double d2_curr = sol[i] - 2.0 * sol_prev[i] + sol_prev2[i];
        double d2_prev = sol_prev[i] - 2.0 * sol_prev2[i] + sol_prev3[i];

        // Three conditions must ALL hold to flag ringing:
        // 1. Sign alternation in second differences (d2_curr and d2_prev
        //    have opposite signs).
        // 2. Amplitude exceeds the convergence tolerance.
        // 3. The second difference is a significant fraction of the first
        //    difference.  Physical oscillations with many steps per cycle
        //    have |d2| << |d1|, while trap ringing (Nyquist-rate) has
        //    |d2| ~ |d1|.  We require |d2| > 0.5 * |d1|.
        double tol = opts.reltol * std::abs(sol[i]) + opts.vntol;
        if (d2_curr * d2_prev < 0.0 &&
            std::abs(d2_curr) > tol &&
            std::abs(d2_prev) > tol) {
            // Check ratio of second to first difference
            double d1_curr = sol[i] - sol_prev[i];
            double d1_prev = sol_prev[i] - sol_prev2[i];
            double d1_max = std::max(std::abs(d1_curr), std::abs(d1_prev));
            if (d1_max < 1e-30) d1_max = 1e-30;  // avoid division by zero
            double ratio = std::max(std::abs(d2_curr), std::abs(d2_prev)) / d1_max;
            if (ratio > 0.5) {
                ringing_detected_ = true;
                ringing_cooldown_ = 3;  // stay on Gear-2 for at least 3 steps
                return;
            }
        }
    }
}

void TimeStepController::tick_cooldown() {
    if (ringing_cooldown_ > 0 && !ringing_detected_) {
        --ringing_cooldown_;
    }
}

} // namespace neospice
