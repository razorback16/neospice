#pragma once
#include <functional>

namespace neospice {

// The owning typed model card keeps updates alive for exactly as long as the
// UCB model they modify. No global registry or dangling model-address cache.
struct ModelCardRuntime {
    std::function<void(double)> update_model_temperature;
    void prepare_model_temperature(double temperature_kelvin) {
        if (update_model_temperature)
            update_model_temperature(temperature_kelvin - 273.15);
    }
};

} // namespace neospice
