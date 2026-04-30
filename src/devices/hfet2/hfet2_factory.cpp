#include "hfet2_device.hpp"
#include "hfet2_model_card.hpp"
#include "devices/device_registry.hpp"

namespace neospice {

void register_hfet2(DeviceRegistry& reg) {
    // Model card factory — level 6
    reg.add_model_factory({
        "hfet", 6, 0,
        [](const ModelCard& card) -> std::unique_ptr<Circuit::ModelCardHolder> {
            return std::make_unique<Circuit::TypedModelCardHolder<HFET2ModelCard>>(
                to_hfet2_card(card), card.name, card.type);
        }
    });

    // Device builder — 3 nodes: drain, gate, source
    reg.add_device_builder({
        'z', 0,
        [](std::string_view name,
           std::span<const int32_t> nodes,
           const std::unordered_map<std::string, double>& /*params*/,
           Circuit::ModelCardHolder& holder) -> std::unique_ptr<Device> {
            auto* h = dynamic_cast<Circuit::TypedModelCardHolder<HFET2ModelCard>*>(&holder);
            if (!h) return nullptr;
            HFET2Device::Geom geom;
            return HFET2Device::make(std::string(name), nodes[0], nodes[1],
                                     nodes[2], geom, *h->card);
        }
    });
}

} // namespace neospice
