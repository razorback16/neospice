#pragma once

#include "core/circuit.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace neospice {

// XSPICE MIFload convergence limiter used by spice2poly code models.
// Returns true when at least one input had to be clipped this load.
inline bool xspice_limit_analog_inputs(std::vector<double>& inputs,
                                       std::vector<double>& last_inputs,
                                       double* state0, double* state1) {
    // Direct device-evaluation unit tests do not bind Circuit state buffers.
    if (!state0) return false;
    if (last_inputs.size() != inputs.size())
        last_inputs.assign(inputs.size(), 0.0);
    constexpr int MODEINITJCT_BIT  = 0x200;
    constexpr int MODEINITTRAN_BIT = 0x1000;
    constexpr int MODEINITPRED_BIT = 0x2000;
    constexpr double relative_step = 0.25;
    constexpr double absolute_step = 0.1;

    const int mode = tls_integrator_ctx ? tls_integrator_ctx->mode : 0;
    bool limited = false;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        double value;
        if (mode & MODEINITJCT_BIT) {
            value = 0.0;
        } else if (mode & (MODEINITTRAN_BIT | MODEINITPRED_BIT)) {
            value = state1 ? state1[i] : 0.0;
        } else {
            const double last = last_inputs[i];
            value = inputs[i];
            const double step = std::max(std::abs(last) * relative_step,
                                         absolute_step);
            if (std::abs(value - last) > step) {
                value = last + std::copysign(step, value - last);
                limited = true;
            }
        }
        inputs[i] = value;
        last_inputs[i] = value;
        if (state0) state0[i] = value;
    }
    return limited;
}

} // namespace neospice
