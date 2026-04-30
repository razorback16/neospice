#include "vbic_device.hpp"
#include "vbic_model_card.hpp"
#include "devices/device_registry.hpp"

namespace neospice {

void register_vbic(DeviceRegistry& reg) {
    // Model card factory — register for levels 4, 9, 12, 13
    auto factory = [](const ModelCard& card) -> std::unique_ptr<Circuit::ModelCardHolder> {
        return std::make_unique<Circuit::TypedModelCardHolder<VBICModelCard>>(
            to_vbic_card(card), card.name, card.type);
    };
    reg.add_model_factory({"bjt", 4, 0, factory});
    reg.add_model_factory({"bjt", 9, 0, factory});
    reg.add_model_factory({"bjt", 12, 0, factory});
    reg.add_model_factory({"bjt", 13, 0, factory});

    // Device builder
    reg.add_device_builder({
        'q', 0,
        [](std::string_view name,
           std::span<const int32_t> nodes,
           const std::unordered_map<std::string, double>& /*params*/,
           Circuit::ModelCardHolder& holder) -> std::unique_ptr<Device> {
            auto* h = dynamic_cast<Circuit::TypedModelCardHolder<VBICModelCard>*>(&holder);
            if (!h) return nullptr;
            VBICDevice::Geom geom;
            return VBICDevice::make(std::string(name), nodes[0], nodes[1],
                                    nodes[2], nodes[3], geom, *h->card);
        }
    });
}

} // namespace neospice
