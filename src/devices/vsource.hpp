#pragma once
#include "devices/device.hpp"
#include <cmath>
#include <complex>
#include <vector>

namespace neospice {

// ---------------------------------------------------------------------------
// Shared source-function types (used by VSource and ISource)
// ---------------------------------------------------------------------------

enum class SourceFunction { DC, PULSE, SIN, PWL, EXP, SFFM, AM };

struct PulseParams {
    double v1 = 0, v2 = 0, td = 0, tr = -1, tf = -1, pw = -1, per = -1;
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
    double sa = 0, oc = 0, fm = -1, fc = -1, td = 0;
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

    /// Return the source function type (DC, PULSE, SIN, etc.).
    SourceFunction source_function() const { return func_; }

    // Device interface
    int32_t extra_vars() const override { return 1; }
    void assign_branch_index(int32_t& next) override {
        set_branch_index(next); next += extra_vars();
    }
    std::vector<std::string> output_currents() const override;

    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
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
    SinParams      sin_;
    PwlParams      pwl_;
    ExpParams      exp_;
    SffmParams     sffm_;
    AmParams       am_;
    double         current_time_ = 0.0;

    // Cached offsets (assigned after pattern is built)
    MatrixOffset off_np_branch_ = -1;  // (np, branch)
    MatrixOffset off_nn_branch_ = -1;  // (nn, branch)
    MatrixOffset off_branch_np_ = -1;  // (branch, np)
    MatrixOffset off_branch_nn_ = -1;  // (branch, nn)
};

} // namespace neospice
