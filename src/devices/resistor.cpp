#include "devices/resistor.hpp"
#include "core/types.hpp"
#include "core/circuit.hpp"

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
    double /*freq*/, const std::vector<double>& /*dc_solution*/) const {
    // Thermal noise: i²_noise = 4kT * G = 4kT / R  (A²/Hz)
    // Use sim_temp_ cached from the last evaluate() call so the noise
    // reflects the actual simulation temperature rather than T_NOMINAL.
    const double G = 1.0 / resistance_;
    const double spectral_density = 4.0 * BOLTZMANN * sim_temp_ * G;
    return {{np_, nn_, spectral_density}};
}

} // namespace neospice
