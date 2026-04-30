#include "mos9_device.hpp"
#include "mos9_model_card.hpp"
#include "devices/device_registry.hpp"

namespace neospice {

void register_mos9(DeviceRegistry& reg) {
    // Model card factory — level 9
    reg.add_model_factory({
        "mos", 9, 0,
        [](const ModelCard& card) -> std::unique_ptr<Circuit::ModelCardHolder> {
            return std::make_unique<Circuit::TypedModelCardHolder<MOS9ModelCard>>(
                to_mos9_card(card), card.name, card.type);
        }
    });

    // Device builder
    reg.add_device_builder({
        'm', 0,
        [](std::string_view name,
           std::span<const int32_t> nodes,
           const std::unordered_map<std::string, double>& params,
           Circuit::ModelCardHolder& holder) -> std::unique_ptr<Device> {
            auto* h = dynamic_cast<Circuit::TypedModelCardHolder<MOS9ModelCard>*>(&holder);
            if (!h) return nullptr;
            MOS9Device::Geom geom;
            if (auto it = params.find("w"); it != params.end()) geom.W = it->second;
            if (auto it = params.find("l"); it != params.end()) geom.L = it->second;
            return MOS9Device::make(std::string(name), nodes[0], nodes[1],
                                    nodes[2], nodes[3], geom, *h->card);
        }
    });
}

} // namespace neospice
