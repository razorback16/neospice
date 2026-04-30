#include "hisimhv_device.hpp"
#include "hisimhv_model_card.hpp"
#include "devices/device.hpp"
#include "devices/device_registry.hpp"

namespace neospice {

void register_hisimhv(DeviceRegistry& reg) {
    // Model card factory — level 73
    reg.add_model_factory({
        "mos", 73, 0,
        [](const ModelCard& card) -> std::unique_ptr<Circuit::ModelCardHolder> {
            return std::make_unique<Circuit::TypedModelCardHolder<HSMHVModelCard>>(
                to_hisimhv_card(card), card.name, card.type);
        }
    });

    // Device builder — HSMHV has an extra substrate node
    // make(name, nd, ng, ns, nb, n_sub, geom, card)
    // where n_sub = GROUND_INTERNAL - 1 (sentinel)
    reg.add_device_builder({
        'm', 0,
        [](std::string_view name,
           std::span<const int32_t> nodes,
           const std::unordered_map<std::string, double>& params,
           Circuit::ModelCardHolder& holder) -> std::unique_ptr<Device> {
            auto* h = dynamic_cast<Circuit::TypedModelCardHolder<HSMHVModelCard>*>(&holder);
            if (!h) return nullptr;
            HSMHVDevice::Geom geom;
            if (auto it = params.find("w"); it != params.end()) geom.W = it->second;
            if (auto it = params.find("l"); it != params.end()) geom.L = it->second;
            return HSMHVDevice::make(std::string(name), nodes[0], nodes[1],
                                     nodes[2], nodes[3], GROUND_INTERNAL - 1,
                                     geom, *h->card);
        }
    });
}

} // namespace neospice
