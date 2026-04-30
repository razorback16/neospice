#include "mes_device.hpp"
#include "mes_model_card.hpp"
#include "devices/device_registry.hpp"

namespace neospice {

void register_mes(DeviceRegistry& reg) {
    // Model card factory — level 0 (default for nmf/pmf)
    reg.add_model_factory({
        "mes", 0, 0,
        [](const ModelCard& card) -> std::unique_ptr<Circuit::ModelCardHolder> {
            return std::make_unique<Circuit::TypedModelCardHolder<MESModelCard>>(
                to_mes_card(card), card.name, card.type);
        }
    });

    // Device builder — 3 nodes: drain, gate, source
    reg.add_device_builder({
        'z', 0,
        [](std::string_view name,
           std::span<const int32_t> nodes,
           const std::unordered_map<std::string, double>& /*params*/,
           Circuit::ModelCardHolder& holder) -> std::unique_ptr<Device> {
            auto* h = dynamic_cast<Circuit::TypedModelCardHolder<MESModelCard>*>(&holder);
            if (!h) return nullptr;
            MESDevice::Geom geom;
            return MESDevice::make(std::string(name), nodes[0], nodes[1],
                                   nodes[2], geom, *h->card);
        }
    });
}

} // namespace neospice
