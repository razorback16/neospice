#pragma once
#include "devices/device.hpp"
#include "devices/vsource.hpp"  // for PulseParams, SinParams, SourceFunction

namespace neospice {

// ---------------------------------------------------------------------------
// ISource — ideal current source (RHS-only stamp, no branch variable)
// ---------------------------------------------------------------------------

class ISource : public Device {
public:
    ISource(std::string name, int32_t node_pos, int32_t node_neg, double dc_value);

    /// AC analysis parameters.
    void set_ac(double mag, double phase_deg = 0.0);
    double ac_mag() const { return ac_mag_; }
    double ac_phase_rad() const { return ac_phase_deg_ * (M_PI / 180.0); }

    int32_t pos_node() const { return np_; }
    int32_t neg_node() const { return nn_; }

    /// Time-domain waveforms.
    void set_pulse(PulseParams p);
    void set_sin(SinParams p);
    void set_pwl(PwlParams p);

    /// Resolve unspecified PULSE/SIN defaults using .tran parameters.
    void resolve_defaults(double tstep, double tstop);

    /// Called before evaluate() during transient analysis.
    void set_time(double t) { current_time_ = t; }

    /// Evaluate the source value at time t.
    double value_at(double t) const;

    /// Return source breakpoints in (tstart, tstop].
    std::vector<double> get_breakpoints(double tstart, double tstop) const;

    // Device interface — no matrix entries, RHS only
    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;

private:
    int32_t np_;       // positive node (conventional current exits here)
    int32_t nn_;       // negative node (conventional current enters here)
    double  dc_value_;

    // AC
    double ac_mag_       = 0.0;
    double ac_phase_deg_ = 0.0;

    // Transient
    SourceFunction func_ = SourceFunction::DC;
    PulseParams    pulse_;
    SinParams      sin_;
    PwlParams      pwl_;
    double         current_time_ = 0.0;
};

} // namespace neospice
