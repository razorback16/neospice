#include "jfet2_device.hpp"
#include "jfet2_model_card.hpp"
#include "devices/device_registry.hpp"

namespace neospice {

void register_jfet2(DeviceRegistry& reg) {
    // Model card factory — level 2
    reg.add_model_factory({
        "jfet", 2, 0,
        [](const ModelCard& card) -> std::unique_ptr<Circuit::ModelCardHolder> {
            return std::make_unique<Circuit::TypedModelCardHolder<JFET2ModelCard>>(
                to_jfet2_card(card), card.name, card.type);
        }
    });

    // Device builder
    reg.add_device_builder({
        'j', 0,
        [](std::string_view name,
           std::span<const int32_t> nodes,
           const std::unordered_map<std::string, double>& /*params*/,
           Circuit::ModelCardHolder& holder) -> std::unique_ptr<Device> {
            auto* h = dynamic_cast<Circuit::TypedModelCardHolder<JFET2ModelCard>*>(&holder);
            if (!h) return nullptr;
            JFET2Device::Geom geom;
            return JFET2Device::make(std::string(name), nodes[0], nodes[1],
                                     nodes[2], geom, *h->card);
        }
    });
}

} // namespace neospice
