#include "devices/isource.hpp"
#include <cmath>

namespace cudaspice {

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

} // namespace cudaspice
