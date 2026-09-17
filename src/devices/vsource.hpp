#pragma once
#include "devices/device.hpp"
#include <span>
#include <cmath>
#include <complex>
#include <optional>
#include <vector>

namespace neospice {

// ---------------------------------------------------------------------------
// Shared source-function types (used by VSource and ISource)
// ---------------------------------------------------------------------------

enum class SourceFunction { DC, PULSE, SIN, PWL, EXP, SFFM, AM };

struct PulseParams {
    double v1 = 0, v2 = 0, td = 0, tr = -1, tf = -1, pw = -1, per = -1;

    // After resolve_defaults(): preserve ngspice VSRC/ISRC period reduction,
    // endpoint ordering and arithmetic order, including near-zero ramps.
    double value_at(double t) const {
        double local = t - td;
        if (per > 0 && local > per)
            local -= per * std::floor(local / per);
        if (local <= 0 || local >= tr + pw + tf) return v1;
        if (local >= tr && local <= tr + pw) return v2;
        if (local > 0 && local < tr)
            return v1 + (v2 - v1) * local / tr;
        return v2 + (v1 - v2) * (local - (tr + pw)) / tf;
    }

    // ngspice VSRC/ISRCaccept: calculate the next corner from the accepted
    // time, preserving floating-point operation order and minimum spacing.
    double next_breakpoint(double t, double min_break) const {
        double local = t - td;
        if (per > 0 && local >= per)
            local -= per * std::floor(local / per);
        const double adjusted = local + min_break;
        double wait;
        if (adjusted < 0) wait = -local;
        else if (adjusted < tr) wait = tr - local;
        else if (adjusted < tr + pw) wait = tr + pw - local;
        else if (adjusted < tr + pw + tf) wait = tr + pw + tf - local;
        else wait = per - local;
        return t + wait;
    }
};

struct SinParams {
    double v0 = 0, va = 0, freq = -1, td = 0, theta = 0, phase = 0;
};

struct PwlParams {
    std::vector<std::pair<double, double>> points;  // (time, value) pairs
};

struct ExpParams {
    double v1 = 0, v2 = 0, td1 = 0, tau1 = -1, td2 = -1, tau2 = -1;
};

struct SffmParams {
    double vo = 0, va = 0, fc = -1, mdi = 0, fs = -1;
};

struct AmParams {
    // ngspice 47: output offset, modulation offset/amplitude, frequencies,
    // delay and independent phases in degrees. Omission differs from zero.
    double vo = 0, vmo = 0, vma = 1;
    std::optional<double> fm, fc;
    double td = 0, phasem = 0, phasec = 0;

    double value_at(double t, double tstop) const {
        const double time = t - td;
        if (time <= 0) return 0.0;
        const double mod_freq = fm.value_or(tstop > 0 ? 5.0 / tstop : 0.0);
        const double carrier_freq = fc.value_or(tstop > 0 ? 500.0 / tstop : 0.0);
        return vo + (vmo + vma * std::sin(2.0 * M_PI * mod_freq * time + phasem * M_PI / 180.0))
            * std::sin(2.0 * M_PI * carrier_freq * time + phasec * M_PI / 180.0);
    }
};

// ---------------------------------------------------------------------------
// VSource — ideal voltage source with MNA branch variable
// ---------------------------------------------------------------------------

class VSource : public Device {
public:
    VSource(std::string name, int32_t node_pos, int32_t node_neg, double dc_value);

    /// Assign the branch (extra) variable index in the MNA system.
    void set_branch_index(int32_t idx);
    int32_t branch_index() const override { return branch_idx_; }
    void apply_ac_excitation(std::vector<std::complex<double>>& ac_rhs,
                             int32_t n) override;

    /// Node accessors (needed by .tf analysis).
    int32_t pos_node() const { return np_; }
    int32_t neg_node() const { return nn_; }

    /// AC analysis parameters.
    void set_ac(double mag, double phase_deg = 0.0);
    double ac_mag() const { return ac_mag_; }
    double ac_phase_rad() const { return ac_phase_deg_ * (M_PI / 180.0); }

    /// Time-domain waveforms.
    void set_pulse(PulseParams p);
    void set_sin(SinParams p);
    void set_pwl(PwlParams p);
    void set_exp(ExpParams p);
    void set_sffm(SffmParams p);
    void set_am(AmParams p);

    /// Override the DC value (used during DC sweep analysis).
    void set_dc_value(double v) { dc_value_ = v; }
    double dc_value() const { return dc_value_; }

    /// Whether an explicit DC value was given (ngspice VSRCdcGiven). When true,
    /// the DC operating point and DC-transfer-curve solves use dc_value_ rather
    /// than the transient waveform's time=0 value.
    void set_dc_given(bool b) { dc_given_ = b; }
    bool dc_given() const { return dc_given_; }

    std::vector<int32_t> external_nodes() const override { return {np_, nn_}; }
    std::optional<double> primary_value() const override { return dc_value_; }
    bool set_value(double value) override { dc_value_ = value; return true; }

    /// Called before evaluate() during transient analysis.
    void set_time(double t) { current_time_ = t; }

    /// Evaluate the source value at time t.
    double value_at(double t) const;

    /// Resolve unspecified PULSE/SIN defaults using .tran parameters.
    /// ngspice: TR/TF default to tstep, PW/PER default to tstop, FREQ to 1/tstop.
    /// Also treats explicit 0 as "unspecified" (matching ngspice behaviour).
    void resolve_defaults(double tstep, double tstop);

    /// Return source breakpoints in (tstart, tstop].
    std::vector<double> get_breakpoints(double tstart, double tstop) const;

    /// Request the next PULSE corner after an accepted point. Reset on each
    /// resolve_defaults() so a reused circuit starts with a fresh schedule.
    std::optional<double> accept_pulse_breakpoint(double t, double min_break) {
        if (func_ != SourceFunction::PULSE || pulse_.per <= 0 || t < pulse_next_request_)
            return std::nullopt;
        const double next = pulse_.next_breakpoint(t, min_break);
        pulse_next_request_ = next - min_break;
        return next;
    }

    /// Return the source function type (DC, PULSE, SIN, etc.).
    SourceFunction source_function() const { return func_; }

    // Device interface
    int32_t extra_vars() const override { return 1; }
    void assign_branch_index(int32_t& next) override {
        if (branch_index() < 0) {
            set_branch_index(next); next += extra_vars();
        }
    }
    std::vector<std::string> output_currents() const override;

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::span<double> rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

private:
    int32_t np_;           // positive node (GROUND_INTERNAL = -1)
    int32_t nn_;           // negative node (GROUND_INTERNAL = -1)
    double  dc_value_;
    bool    dc_given_ = false;  // ngspice VSRCdcGiven
    int32_t branch_idx_ = -1;  // index of the branch current variable

    // AC
    double ac_mag_       = 0.0;
    double ac_phase_deg_ = 0.0;

    // Transient
    SourceFunction func_ = SourceFunction::DC;
    PulseParams    pulse_;
    double pulse_next_request_ = 0.0;
    SinParams      sin_;
    PwlParams      pwl_;
    ExpParams      exp_;
    SffmParams     sffm_;
    AmParams       am_;
    double         am_tstop_ = 0.0;
    double         current_time_ = 0.0;

    // Cached offsets (assigned after pattern is built)
    MatrixOffset off_np_branch_ = -1;  // (np, branch)
    MatrixOffset off_nn_branch_ = -1;  // (nn, branch)
    MatrixOffset off_branch_np_ = -1;  // (branch, np)
    MatrixOffset off_branch_nn_ = -1;  // (branch, nn)
};

} // namespace neospice
