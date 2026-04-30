#include "jfet_device.hpp"
#include "jfet_model_card.hpp"
#include "devices/device_registry.hpp"

namespace neospice {

void register_jfet(DeviceRegistry& reg) {
    // Model card factory — level 1 (default for JFET)
    reg.add_model_factory({
        "jfet", 1, 0,
        [](const ModelCard& card) -> std::unique_ptr<Circuit::ModelCardHolder> {
            return std::make_unique<Circuit::TypedModelCardHolder<JFETModelCard>>(
                to_jfet_card(card), card.name, card.type);
        }
    });

    // Device builder
    reg.add_device_builder({
        'j', 0,
        [](std::string_view name,
           std::span<const int32_t> nodes,
           const std::unordered_map<std::string, double>& /*params*/,
           Circuit::ModelCardHolder& holder) -> std::unique_ptr<Device> {
            auto* h = dynamic_cast<Circuit::TypedModelCardHolder<JFETModelCard>*>(&holder);
            if (!h) return nullptr;
            JFETDevice::Geom geom;
            return JFETDevice::make(std::string(name), nodes[0], nodes[1],
                                    nodes[2], geom, *h->card);
        }
    });
}

} // namespace neospice
