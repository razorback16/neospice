#include "devices/vsource.hpp"
#include <cmath>
#include <stdexcept>

namespace neospice {

VSource::VSource(std::string name, int32_t node_pos, int32_t node_neg, double dc_value)
    : Device(std::move(name)), np_(node_pos), nn_(node_neg), dc_value_(dc_value)
{}

void VSource::set_branch_index(int32_t idx) {
    branch_idx_ = idx;
}

void VSource::set_ac(double mag, double phase_deg) {
    ac_mag_       = mag;
    ac_phase_deg_ = phase_deg;
}

void VSource::set_pulse(PulseParams p) {
    func_  = SourceFunction::PULSE;
    pulse_ = p;
}

void VSource::set_sin(SinParams p) {
    func_ = SourceFunction::SIN;
    sin_  = p;
}

double VSource::value_at(double t) const {
    switch (func_) {
    case SourceFunction::DC:
        return dc_value_;

    case SourceFunction::PULSE: {
        const auto& p = pulse_;
        if (t < p.td) return p.v1;
        // Avoid division by zero for zero period
        double t_rel = t - p.td;
        double t_in  = (p.per > 0.0) ? std::fmod(t_rel, p.per) : t_rel;
        if (t_in < p.tr) {
            // Linear rise v1 -> v2
            return p.v1 + (p.v2 - p.v1) * (t_in / p.tr);
        }
        if (t_in < p.tr + p.pw) {
            return p.v2;
        }
        if (t_in < p.tr + p.pw + p.tf) {
            double t_fall = t_in - (p.tr + p.pw);
            // Linear fall v2 -> v1
            return p.v2 + (p.v1 - p.v2) * (t_fall / p.tf);
        }
        return p.v1;
    }

    case SourceFunction::SIN: {
        const auto& s  = sin_;
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

std::vector<std::string> VSource::output_currents() const {
    return { "I(" + name_ + ")" };
}

void VSource::stamp_pattern(SparsityBuilder& builder) const {
    if (branch_idx_ < 0)
        throw std::logic_error("VSource::stamp_pattern called before set_branch_index");
    // (np, branch) and (branch, np)
    stamp_if_not_ground(builder, np_, branch_idx_);
    stamp_if_not_ground(builder, branch_idx_, np_);
    // (nn, branch) and (branch, nn)
    stamp_if_not_ground(builder, nn_, branch_idx_);
    stamp_if_not_ground(builder, branch_idx_, nn_);
}

void VSource::assign_offsets(const SparsityPattern& pattern) {
    off_np_branch_ = offset_if_not_ground(pattern, np_, branch_idx_);
    off_nn_branch_ = offset_if_not_ground(pattern, nn_, branch_idx_);
    off_branch_np_ = offset_if_not_ground(pattern, branch_idx_, np_);
    off_branch_nn_ = offset_if_not_ground(pattern, branch_idx_, nn_);
}

void VSource::evaluate(const std::vector<double>& /*voltages*/,
                       NumericMatrix& mat, std::vector<double>& rhs) {
    // KCL rows: np gets +I_branch contribution, nn gets -I_branch
    add_if_valid(mat, off_np_branch_,  1.0);
    add_if_valid(mat, off_nn_branch_, -1.0);
    // Branch equation: V(np) - V(nn) = V_source
    add_if_valid(mat, off_branch_np_,  1.0);
    add_if_valid(mat, off_branch_nn_, -1.0);
    // RHS for the branch equation row
    if (branch_idx_ >= 0)
        rhs[branch_idx_] += value_at(current_time_);
}

void VSource::ac_stamp(const std::vector<double>& /*voltages*/,
                       NumericMatrix& G, NumericMatrix& /*C*/) {
    // Same ±1 coupling as DC evaluate — stamp into G matrix
    add_if_valid(G, off_np_branch_,  1.0);
    add_if_valid(G, off_nn_branch_, -1.0);
    add_if_valid(G, off_branch_np_,  1.0);
    add_if_valid(G, off_branch_nn_, -1.0);
}

std::vector<double> VSource::get_breakpoints(double tstart, double tstop) const {
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
