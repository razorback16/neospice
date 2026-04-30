#include "bsim4v7_device.hpp"
#include "bsim4v7_model_card.hpp"
#include "devices/device_registry.hpp"

namespace neospice {

void register_bsim4v7(DeviceRegistry& reg) {
    // Model card factory — level 14
    reg.add_model_factory({
        "mos", 14, 0,
        [](const ModelCard& card) -> std::unique_ptr<Circuit::ModelCardHolder> {
            return std::make_unique<Circuit::TypedModelCardHolder<BSIM4v7ModelCard>>(
                to_bsim4_card(card), card.name, card.type);
        }
    });

    // Device builder
    reg.add_device_builder({
        'm', 0,
        [](std::string_view name,
           std::span<const int32_t> nodes,
           const std::unordered_map<std::string, double>& params,
           Circuit::ModelCardHolder& holder) -> std::unique_ptr<Device> {
            auto* h = dynamic_cast<Circuit::TypedModelCardHolder<BSIM4v7ModelCard>*>(&holder);
            if (!h) return nullptr;
            BSIM4v7Device::Geom geom;
            if (auto it = params.find("w"); it != params.end()) geom.W = it->second;
            if (auto it = params.find("l"); it != params.end()) geom.L = it->second;
            return BSIM4v7Device::make(std::string(name), nodes[0], nodes[1],
                                        nodes[2], nodes[3], geom, *h->card);
        }
    });
}

} // namespace neospice
