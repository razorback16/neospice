#include "mos3_device.hpp"
#include "mos3_model_card.hpp"
#include "devices/device_registry.hpp"

namespace neospice {

void register_mos3(DeviceRegistry& reg) {
    // Model card factory — level 3
    reg.add_model_factory({
        "mos", 3, 0,
        [](const ModelCard& card) -> std::unique_ptr<Circuit::ModelCardHolder> {
            return std::make_unique<Circuit::TypedModelCardHolder<MOS3ModelCard>>(
                to_mos3_card(card), card.name, card.type);
        }
    });

    // Device builder
    reg.add_device_builder({
        'm', 0,
        [](std::string_view name,
           std::span<const int32_t> nodes,
           const std::unordered_map<std::string, double>& params,
           Circuit::ModelCardHolder& holder) -> std::unique_ptr<Device> {
            auto* h = dynamic_cast<Circuit::TypedModelCardHolder<MOS3ModelCard>*>(&holder);
            if (!h) return nullptr;
            MOS3Device::Geom geom;
            if (auto it = params.find("w"); it != params.end()) geom.W = it->second;
            if (auto it = params.find("l"); it != params.end()) geom.L = it->second;
            return MOS3Device::make(std::string(name), nodes[0], nodes[1],
                                    nodes[2], nodes[3], geom, *h->card);
        }
    });
}

} // namespace neospice
