#pragma once
#include "devices/device.hpp"
#include "devices/vsource.hpp"  // for PulseParams, SinParams, SourceFunction
#include <complex>

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

    /// Override the DC value (used during sensitivity analysis).
    void set_dc_value(double v) { dc_value_ = v; }
    double dc_value() const { return dc_value_; }

    /// Whether an explicit DC value was given (ngspice ISRCdcGiven). When true,
    /// the DC operating point and DC-transfer-curve solves use dc_value_ rather
    /// than the transient waveform's time=0 value.
    void set_dc_given(bool b) { dc_given_ = b; }
    bool dc_given() const { return dc_given_; }

    std::vector<int32_t> external_nodes() const override { return {np_, nn_}; }
    std::optional<double> primary_value() const override { return dc_value_; }
    bool set_value(double value) override { dc_value_ = value; return true; }

    /// Time-domain waveforms.
    void set_pulse(PulseParams p);
    void set_sin(SinParams p);
    void set_pwl(PwlParams p);
    void set_exp(ExpParams p);
    void set_sffm(SffmParams p);
    void set_am(AmParams p);

    /// Resolve unspecified PULSE/SIN defaults using .tran parameters.
    void resolve_defaults(double tstep, double tstop);

    /// Called before evaluate() during transient analysis.
    void set_time(double t) { current_time_ = t; }

    /// Evaluate the source value at time t.
    double value_at(double t) const;

    /// Return source breakpoints in (tstart, tstop].
    std::vector<double> get_breakpoints(double tstart, double tstop) const;

    /// Return the source function type (DC, PULSE, SIN, etc.).
    SourceFunction source_function() const { return func_; }

    // Device interface — no matrix entries, RHS only
    void apply_ac_excitation(std::vector<std::complex<double>>& ac_rhs,
                             int32_t n) override;
    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;

private:
    int32_t np_;       // positive node (conventional current exits here)
    int32_t nn_;       // negative node (conventional current enters here)
    double  dc_value_;
    bool    dc_given_ = false;  // ngspice ISRCdcGiven

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
};

} // namespace neospice
