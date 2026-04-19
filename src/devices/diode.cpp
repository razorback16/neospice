#include "devices/diode.hpp"
#include <cmath>
#include <algorithm>

namespace neospice {

Diode::Diode(std::string name, int32_t node_anode, int32_t node_cathode,
             const DiodeModel& model)
    : Device(std::move(name)), na_(node_anode), nc_(node_cathode), model_(model)
{}

void Diode::set_temp(double temp) {
    temp_ = temp;
}

// ---------------------------------------------------------------------------
// Sparsity pattern — 2x2 conductance stamp (same as resistor)
// ---------------------------------------------------------------------------

void Diode::stamp_pattern(SparsityBuilder& builder) const {
    stamp_if_not_ground(builder, na_, na_);
    stamp_if_not_ground(builder, na_, nc_);
    stamp_if_not_ground(builder, nc_, na_);
    stamp_if_not_ground(builder, nc_, nc_);
}

void Diode::assign_offsets(const SparsityPattern& pattern) {
    off_aa_ = offset_if_not_ground(pattern, na_, na_);
    off_ac_ = offset_if_not_ground(pattern, na_, nc_);
    off_ca_ = offset_if_not_ground(pattern, nc_, na_);
    off_cc_ = offset_if_not_ground(pattern, nc_, nc_);
}

// ---------------------------------------------------------------------------
// Newton linearization of Shockley diode equation
// ---------------------------------------------------------------------------

void Diode::evaluate(const std::vector<double>& voltages,
                     NumericMatrix& mat, std::vector<double>& rhs) {
    const double vt  = BOLTZMANN * temp_ / CHARGE_Q;
    const double nvt = model_.N * vt;

    // Diode voltage from solution vector
    const double va  = (na_ >= 0) ? voltages[na_] : 0.0;
    const double vc  = (nc_ >= 0) ? voltages[nc_] : 0.0;
    const double vd  = va - vc;

    // Clamp exponent to prevent overflow
    const double evd = std::exp(std::min(vd / nvt, 500.0));
    const double id  = model_.Is * (evd - 1.0);
    const double gd  = (model_.Is / nvt) * evd;

    // Stamp conductance (like resistor)
    add_if_valid(mat, off_aa_,  gd);
    add_if_valid(mat, off_ac_, -gd);
    add_if_valid(mat, off_ca_, -gd);
    add_if_valid(mat, off_cc_,  gd);

    // Norton equivalent current source: ieq = id - gd*vd
    const double ieq = id - gd * vd;
    add_rhs_if_valid(rhs, na_, -ieq);
    add_rhs_if_valid(rhs, nc_,  ieq);

    // Cache for AC / noise analysis
    last_gd_ = gd;
    last_id_ = id;
}

// ---------------------------------------------------------------------------
// Voltage limiting — prevent exponential overflow during Newton iterations
// ---------------------------------------------------------------------------

void Diode::limit_voltages(const std::vector<double>& old_v,
                            std::vector<double>& new_v) {
    const double vt  = BOLTZMANN * temp_ / CHARGE_Q;
    const double nvt = model_.N * vt;

    // Critical voltage: nvt * ln(nvt / (sqrt(2) * Is))
    const double vcrit = nvt * std::log(nvt / (std::sqrt(2.0) * model_.Is));

    const double va_old = (na_ >= 0) ? old_v[na_] : 0.0;
    const double vc_old = (nc_ >= 0) ? old_v[nc_] : 0.0;
    const double vd_old = va_old - vc_old;

    const double va_new = (na_ >= 0) ? new_v[na_] : 0.0;
    const double vc_new = (nc_ >= 0) ? new_v[nc_] : 0.0;
    double vd_new = va_new - vc_new;

    // Apply limiting only when new voltage exceeds vcrit by more than 2*nvt
    if (vd_new > vcrit && std::abs(vd_new - vd_old) > 2.0 * nvt) {
        if (vd_old > vcrit) {
            // Both above vcrit: allow logarithmic step
            vd_new = vd_old + nvt * std::log(1.0 + (vd_new - vd_old) / nvt);
        } else {
            // Old was below vcrit, new is above: clamp to vcrit + nvt
            vd_new = vcrit + nvt;
        }
        // Apply limited vd back to anode node voltage
        if (na_ >= 0) {
            new_v[na_] = vc_new + vd_new;
        }
    }
}

// ---------------------------------------------------------------------------
// AC small-signal stamp
// ---------------------------------------------------------------------------

void Diode::ac_stamp(const std::vector<double>& voltages,
                     NumericMatrix& G, NumericMatrix& C) {
    // G matrix: small-signal conductance (cached from last evaluate())
    add_if_valid(G, off_aa_,  last_gd_);
    add_if_valid(G, off_ac_, -last_gd_);
    add_if_valid(G, off_ca_, -last_gd_);
    add_if_valid(G, off_cc_,  last_gd_);

    // C matrix: junction + diffusion capacitance
    const double va = (na_ >= 0) ? voltages[na_] : 0.0;
    const double vc = (nc_ >= 0) ? voltages[nc_] : 0.0;
    const double vd = va - vc;

    double cj = 0.0;
    if (model_.Cj0 > 0.0) {
        if (vd < 0.0) {
            // Reverse bias: depletion capacitance from Shockley-Read-Hall model
            cj = model_.Cj0 * std::pow(1.0 - vd / model_.Vj, -model_.M);
        } else {
            // Forward bias linearization: use value at vd=0 as approximation
            cj = model_.Cj0;
        }
    }

    // Diffusion capacitance: Cd = Tt * gd
    const double cd = model_.Tt * last_gd_;

    const double ctotal = cj + cd;
    last_cd_ = ctotal;

    // Stamp like conductance into C matrix
    add_if_valid(C, off_aa_,  ctotal);
    add_if_valid(C, off_ac_, -ctotal);
    add_if_valid(C, off_ca_, -ctotal);
    add_if_valid(C, off_cc_,  ctotal);
}

// ---------------------------------------------------------------------------
// Noise sources — shot noise (2qI) + thermal noise (4kTG)
// ---------------------------------------------------------------------------

std::vector<Device::NoiseSource> Diode::noise_sources(
    double freq, const std::vector<double>& /*dc_solution*/) const {
    // Junction shot noise: S = 2 * q * |I_dc|  (A^2/Hz)
    // Series resistance thermal noise (4kT/Rs) omitted — this model has no Rs.
    double spectral_density = 2.0 * CHARGE_Q * std::abs(last_id_);

    // Flicker (1/f) noise: S_flicker = Kf * |Id|^Af / f^Ef
    // Only added if Kf != 0 and frequency is positive.
    if (model_.Kf != 0.0 && freq > 0.0) {
        const double i_abs = std::abs(last_id_);
        const double i_af = (i_abs > 0.0 || model_.Af == 0.0)
                            ? std::pow(i_abs, model_.Af) : 0.0;
        const double s_flicker = model_.Kf * i_af / std::pow(freq, model_.Ef);
        spectral_density += s_flicker;
    }

    return {{na_, nc_, spectral_density}};
}

// ---------------------------------------------------------------------------
// Output current name
// ---------------------------------------------------------------------------

std::vector<std::string> Diode::output_currents() const {
    return { "i(" + name_ + ")" };
}

} // namespace neospice
