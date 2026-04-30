#include "hfet1_device.hpp"
#include "hfet1_model_card.hpp"
#include "devices/device_registry.hpp"

namespace neospice {

void register_hfet1(DeviceRegistry& reg) {
    // Model card factory — level 0 (default for nhfet/phfet) and level 5
    auto factory = [](const ModelCard& card) -> std::unique_ptr<Circuit::ModelCardHolder> {
        return std::make_unique<Circuit::TypedModelCardHolder<HFETAModelCard>>(
            to_hfet1_card(card), card.name, card.type);
    };
    reg.add_model_factory({"hfet", 0, 0, factory});
    reg.add_model_factory({"hfet", 5, 0, factory});

    // Device builder — 3 nodes: drain, gate, source
    reg.add_device_builder({
        'z', 0,
        [](std::string_view name,
           std::span<const int32_t> nodes,
           const std::unordered_map<std::string, double>& /*params*/,
           Circuit::ModelCardHolder& holder) -> std::unique_ptr<Device> {
            auto* h = dynamic_cast<Circuit::TypedModelCardHolder<HFETAModelCard>*>(&holder);
            if (!h) return nullptr;
            HFETADevice::Geom geom;
            return HFETADevice::make(std::string(name), nodes[0], nodes[1],
                                     nodes[2], geom, *h->card);
        }
    });
}

} // namespace neospice
