#include "devices/isource.hpp"
#include <cmath>

namespace neospice {

ISource::ISource(std::string name, int32_t node_pos, int32_t node_neg, double dc_value)
    : Device(std::move(name)), np_(node_pos), nn_(node_neg), dc_value_(dc_value)
{}

void ISource::set_ac(double mag, double phase_deg) {
    ac_mag_       = mag;
    ac_phase_deg_ = phase_deg;
}

void ISource::set_pulse(PulseParams p) {
    func_  = SourceFunction::PULSE;
    pulse_ = p;
}

void ISource::set_sin(SinParams p) {
    func_ = SourceFunction::SIN;
    sin_  = p;
}

void ISource::resolve_defaults(double tstep, double tstop) {
    if (func_ == SourceFunction::PULSE) {
        if (pulse_.tr <= 0) pulse_.tr = tstep;
        if (pulse_.tf <= 0) pulse_.tf = tstep;
        if (pulse_.pw <= 0) pulse_.pw = tstop;
        if (pulse_.per <= 0) pulse_.per = tstop;
    } else if (func_ == SourceFunction::SIN) {
        if (sin_.freq <= 0) sin_.freq = (tstop > 0) ? 1.0 / tstop : 0.0;
    }
}

double ISource::value_at(double t) const {
    switch (func_) {
    case SourceFunction::DC:
        return dc_value_;

    case SourceFunction::PULSE: {
        const auto& p = pulse_;
        if (t < p.td) return p.v1;
        double t_rel = t - p.td;
        double t_in  = (p.per > 0.0) ? std::fmod(t_rel, p.per) : t_rel;
        if (t_in < p.tr) {
            return p.v1 + (p.v2 - p.v1) * (t_in / p.tr);
        }
        if (t_in < p.tr + p.pw) {
            return p.v2;
        }
        if (t_in < p.tr + p.pw + p.tf) {
            double t_fall = t_in - (p.tr + p.pw);
            return p.v2 + (p.v1 - p.v2) * (t_fall / p.tf);
        }
        return p.v1;
    }

    case SourceFunction::SIN: {
        const auto& s    = sin_;
        double phase_rad = s.phase * (M_PI / 180.0);
        if (t < s.td) {
            return s.v0 + s.va * std::sin(phase_rad);
        }
        double dt = t - s.td;
        return s.v0 + s.va * std::sin(2.0 * M_PI * s.freq * dt + phase_rad)
                           * std::exp(-s.theta * dt);
    }
    }
    return dc_value_;
}

// No matrix entries needed for a current source.
void ISource::stamp_pattern(SparsityBuilder& /*builder*/) const {}

void ISource::assign_offsets(const SparsityPattern& /*pattern*/) {}

void ISource::evaluate(const std::vector<double>& /*voltages*/,
                       NumericMatrix& /*mat*/, std::vector<double>& rhs) {
    // Convention: current flows from np to nn through the source.
    // KCL: current leaves np  -> -I at np
    //      current enters nn  -> +I at nn
    double I = value_at(current_time_);
    add_rhs_if_valid(rhs, np_, -I);
    add_rhs_if_valid(rhs, nn_,  I);
}

std::vector<double> ISource::get_breakpoints(double tstart, double tstop) const {
    std::vector<double> bps;
    if (func_ != SourceFunction::PULSE) return bps;

    const auto& p = pulse_;
    if (p.per <= 0.0) return bps;

    int max_periods = static_cast<int>((tstop - p.td) / p.per) + 2;

    for (int k = 0; k < max_periods; ++k) {
        double t0 = p.td + k * p.per;
        if (t0 > tstop) break;

        double edges[] = { t0, t0 + p.tr, t0 + p.tr + p.pw, t0 + p.tr + p.pw + p.tf };
        for (double e : edges) {
            if (e > tstart && e <= tstop) {
                bps.push_back(e);
            }
        }
    }
    return bps;
}

} // namespace neospice
