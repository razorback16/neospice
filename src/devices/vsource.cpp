#include "devices/vsource.hpp"
#include "core/circuit.hpp"
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

void VSource::set_pwl(PwlParams p) {
    func_ = SourceFunction::PWL;
    pwl_  = std::move(p);
}

void VSource::set_exp(ExpParams p) {
    func_ = SourceFunction::EXP;
    exp_  = p;
}

void VSource::set_sffm(SffmParams p) {
    func_ = SourceFunction::SFFM;
    sffm_ = p;
}

void VSource::set_am(AmParams p) {
    func_ = SourceFunction::AM;
    am_   = p;
}

void VSource::resolve_defaults(double tstep, double tstop) {
    if (func_ == SourceFunction::PULSE) {
        // ngspice (vsrcload.c): TR/TF default to CKTstep when 0 or unspecified,
        // PW/PER default to CKTfinalTime when 0 or unspecified.
        if (pulse_.tr <= 0) pulse_.tr = tstep;
        if (pulse_.tf <= 0) pulse_.tf = tstep;
        if (pulse_.pw <= 0) pulse_.pw = tstop;
        if (pulse_.per <= 0) pulse_.per = tstop;
    } else if (func_ == SourceFunction::SIN) {
        // ngspice (vsrcload.c): FREQ defaults to 1/CKTfinalTime when 0 or unspecified.
        if (sin_.freq <= 0) sin_.freq = (tstop > 0) ? 1.0 / tstop : 0.0;
    } else if (func_ == SourceFunction::EXP) {
        // ngspice (vsrcload.c): all EXP params treat explicit 0 as "unspecified".
        // TD1 defaults to tstep, TAU1 to tstep, TD2 to TD1+tstep, TAU2 to tstep.
        if (exp_.td1  <= 0) exp_.td1  = tstep;
        if (exp_.tau1 <= 0) exp_.tau1 = tstep;
        if (exp_.td2  <= 0) exp_.td2  = exp_.td1 + tstep;
        if (exp_.tau2 <= 0) exp_.tau2 = tstep;
    } else if (func_ == SourceFunction::SFFM) {
        // ngspice (vsrcload.c): FC/FS default to 1/tstop when 0 or unspecified.
        if (sffm_.fc <= 0) sffm_.fc = (tstop > 0) ? 1.0 / tstop : 0.0;
        if (sffm_.fs <= 0) sffm_.fs = (tstop > 0) ? 1.0 / tstop : 0.0;
    } else if (func_ == SourceFunction::AM) {
        // ngspice (vsrcload.c): FM/FC default to 1/tstop when 0 or unspecified.
        if (am_.fm <= 0) am_.fm = (tstop > 0) ? 1.0 / tstop : 0.0;
        if (am_.fc <= 0) am_.fc = (tstop > 0) ? 1.0 / tstop : 0.0;
    }
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

    case SourceFunction::PWL: {
        const auto& pts = pwl_.points;
        if (pts.empty()) return dc_value_;
        if (t <= pts.front().first) return pts.front().second;
        if (t >= pts.back().first) return pts.back().second;
        size_t hi = 0;
        for (hi = 1; hi < pts.size(); ++hi) {
            if (pts[hi].first >= t) break;
        }
        size_t lo = hi - 1;
        double t0 = pts[lo].first, v0 = pts[lo].second;
        double t1 = pts[hi].first, v1 = pts[hi].second;
        if (t1 == t0) return v1;  // degenerate segment
        double frac = (t - t0) / (t1 - t0);
        return v0 + (v1 - v0) * frac;
    }

    case SourceFunction::EXP: {
        const auto& e = exp_;
        if (t <= e.td1) {
            return e.v1;
        } else if (t <= e.td2) {
            return e.v1 + (e.v2 - e.v1) * (1.0 - std::exp(-(t - e.td1) / e.tau1));
        } else {
            return e.v1 + (e.v2 - e.v1) * (1.0 - std::exp(-(t - e.td1) / e.tau1))
                        + (e.v1 - e.v2) * (1.0 - std::exp(-(t - e.td2) / e.tau2));
        }
    }

    case SourceFunction::SFFM: {
        const auto& s = sffm_;
        return s.vo + s.va * std::sin(2.0 * M_PI * s.fc * t
                                      + s.mdi * std::sin(2.0 * M_PI * s.fs * t));
    }

    case SourceFunction::AM: {
        const auto& a = am_;
        if (t < a.td) return 0.0;
        double dt = t - a.td;
        return a.sa * (a.oc + std::sin(2.0 * M_PI * a.fm * dt))
                     * std::sin(2.0 * M_PI * a.fc * dt);
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
    // RHS for the branch equation row.
    // Match ngspice vsrcload.c: in the DC operating-point and DC-transfer-curve
    // solves, a source with an explicit DC value uses that value instead of the
    // transient waveform's time=0 value. MODEDCOP=0x10, MODEDCTRANCURVE=0x40.
    // MODETRANOP (0x20) is intentionally excluded: ngspice evaluates the
    // transient function at time=0 for the transient preamble.
    if (branch_idx_ >= 0) {
        double val;
        if (dc_given_ && func_ != SourceFunction::DC &&
            tls_integrator_ctx &&
            (tls_integrator_ctx->mode & (0x10 | 0x40))) {
            val = dc_value_;
        } else {
            val = value_at(current_time_);
        }
        if (tls_integrator_ctx && tls_integrator_ctx->options)
            val *= tls_integrator_ctx->options->src_fact;
        rhs[branch_idx_] += val;
    }
}

void VSource::ac_stamp(const std::vector<double>& /*voltages*/,
                       NumericMatrix& G, NumericMatrix& /*C*/) {
    // Same ±1 coupling as DC evaluate — stamp into G matrix
    add_if_valid(G, off_np_branch_,  1.0);
    add_if_valid(G, off_nn_branch_, -1.0);
    add_if_valid(G, off_branch_np_,  1.0);
    add_if_valid(G, off_branch_nn_, -1.0);
}

void VSource::apply_ac_excitation(std::vector<std::complex<double>>& ac_rhs,
                                  int32_t n) {
    if (ac_mag_ != 0.0) {
        int32_t br = branch_idx_;
        if (br >= 0 && br < n) {
            ac_rhs[br] = std::polar(ac_mag_, ac_phase_deg_ * (M_PI / 180.0));
        }
    }
}

std::vector<double> VSource::get_breakpoints(double tstart, double tstop) const {
    std::vector<double> bps;

    if (func_ == SourceFunction::PWL) {
        for (const auto& [tp, vp] : pwl_.points) {
            if (tp > tstart && tp <= tstop) {
                bps.push_back(tp);
            }
        }
        return bps;
    }

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
