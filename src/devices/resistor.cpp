#include "devices/resistor.hpp"
#include "core/types.hpp"
#include "core/circuit.hpp"
#include <cmath>

namespace neospice {

Resistor::Resistor(std::string name, int32_t node_pos, int32_t node_neg, double resistance)
    : Device(std::move(name)), np_(node_pos), nn_(node_neg), resistance_(resistance)
{}

void Resistor::stamp_pattern(SparsityBuilder& builder) const {
    stamp_if_not_ground(builder, np_, np_);
    stamp_if_not_ground(builder, np_, nn_);
    stamp_if_not_ground(builder, nn_, np_);
    stamp_if_not_ground(builder, nn_, nn_);
}

void Resistor::assign_offsets(const SparsityPattern& pattern) {
    off_pp_ = offset_if_not_ground(pattern, np_, np_);
    off_pn_ = offset_if_not_ground(pattern, np_, nn_);
    off_np_ = offset_if_not_ground(pattern, nn_, np_);
    off_nn_ = offset_if_not_ground(pattern, nn_, nn_);
}

void Resistor::evaluate(const std::vector<double>& /*voltages*/,
                        NumericMatrix& mat, std::vector<double>& /*rhs*/) {
    // Cache simulation temperature for use in noise_sources().
    if (const IntegratorCtx* ic = tls_integrator_ctx) {
        if (ic->options) sim_temp_ = ic->options->temp;
    }

    const double g = 1.0 / resistance_;
    add_if_valid(mat, off_pp_,  g);
    add_if_valid(mat, off_pn_, -g);
    add_if_valid(mat, off_np_, -g);
    add_if_valid(mat, off_nn_,  g);
}

void Resistor::ac_stamp(const std::vector<double>& /*voltages*/,
                        NumericMatrix& G, NumericMatrix& /*C*/) {
    // Pure conductance: same stamp as DC, no capacitance contribution
    const double g = 1.0 / resistance_;
    add_if_valid(G, off_pp_,  g);
    add_if_valid(G, off_pn_, -g);
    add_if_valid(G, off_np_, -g);
    add_if_valid(G, off_nn_,  g);
}

std::vector<Device::NoiseSource> Resistor::noise_sources(
    double freq, const std::vector<double>& dc_solution) const {
    const double G = 1.0 / resistance_;
    double spectral_density = 4.0 * BOLTZMANN * sim_temp_ * G;

    // Flicker (1/f) noise: S_flicker = Kf * |I_dc|^Af / f^Ef
    if (noise_kf != 0.0 && freq > 0.0) {
        const double v_np = (np_ >= 0) ? dc_solution[np_] : 0.0;
        const double v_nn = (nn_ >= 0) ? dc_solution[nn_] : 0.0;
        const double i_dc = (v_np - v_nn) / resistance_;
        const double i_abs = std::abs(i_dc);
        const double i_af = (i_abs > 0.0 || noise_af == 0.0)
                            ? std::pow(i_abs, noise_af) : 0.0;
        const double s_flicker = noise_kf * i_af / std::pow(freq, noise_ef);
        spectral_density += s_flicker;
    }

    return {{np_, nn_, spectral_density}};
}

} // namespace neospice
