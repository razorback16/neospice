#include "bsim3v32_device.hpp"
#include "bsim3v32_model_card.hpp"
#include "devices/device_registry.hpp"

namespace neospice {

void register_bsim3v32(DeviceRegistry& reg) {
    // No model factory — BSIM3 factory handles level 8/49 with version gating

    // Device builder — only builds BSIM3v32
    reg.add_device_builder({
        'm', 0,
        [](std::string_view name,
           std::span<const int32_t> nodes,
           const std::unordered_map<std::string, double>& params,
           Circuit::ModelCardHolder& holder) -> std::unique_ptr<Device> {
            auto* h = dynamic_cast<Circuit::TypedModelCardHolder<BSIM3v32ModelCard>*>(&holder);
            if (!h) return nullptr;
            BSIM3v32Device::Geom geom;
            if (auto it = params.find("w"); it != params.end()) geom.W = it->second;
            if (auto it = params.find("l"); it != params.end()) geom.L = it->second;
            return BSIM3v32Device::make(std::string(name), nodes[0], nodes[1],
                                         nodes[2], nodes[3], geom, *h->card);
        }
    });
}

} // namespace neospice
