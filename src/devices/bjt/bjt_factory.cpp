#include "bjt_device.hpp"
#include "bjt_model_card.hpp"
#include "devices/device_registry.hpp"

namespace neospice {

void register_bjt(DeviceRegistry& reg) {
    // Model card factory — level 1 (default for BJT)
    reg.add_model_factory({
        "bjt", 1, 0,
        [](const ModelCard& card) -> std::unique_ptr<Circuit::ModelCardHolder> {
            return std::make_unique<Circuit::TypedModelCardHolder<BJTModelCard>>(
                to_bjt_card(card), card.name, card.type);
        }
    });

    // Device builder
    reg.add_device_builder({
        'q', 0,
        [](std::string_view name,
           std::span<const int32_t> nodes,
           const std::unordered_map<std::string, double>& /*params*/,
           Circuit::ModelCardHolder& holder) -> std::unique_ptr<Device> {
            auto* h = dynamic_cast<Circuit::TypedModelCardHolder<BJTModelCard>*>(&holder);
            if (!h) return nullptr;
            BJTDevice::Geom geom;
            return BJTDevice::make(std::string(name), nodes[0], nodes[1],
                                   nodes[2], nodes[3], geom, *h->card);
        }
    });
}

} // namespace neospice
