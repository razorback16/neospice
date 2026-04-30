#include "bsimsoi_device.hpp"
#include "bsimsoi_model_card.hpp"
#include "devices/device.hpp"
#include "devices/device_registry.hpp"

namespace neospice {

void register_bsimsoi(DeviceRegistry& reg) {
    // Model card factory — levels 10 and 58
    auto factory = [](const ModelCard& card) -> std::unique_ptr<Circuit::ModelCardHolder> {
        return std::make_unique<Circuit::TypedModelCardHolder<B4SOIModelCard>>(
            to_bsimsoi_card(card), card.name, card.type);
    };
    reg.add_model_factory({"mos", 10, 0, factory});
    reg.add_model_factory({"mos", 58, 0, factory});

    // Device builder — B4SOI has extra node args:
    // make(name, nd, ng, ns, ne, np, nb, geom, card)
    // The typed API passes 4 nodes (d,g,s,b) where b maps to n_e,
    // and n_p and n_b are GROUND_INTERNAL.
    reg.add_device_builder({
        'm', 0,
        [](std::string_view name,
           std::span<const int32_t> nodes,
           const std::unordered_map<std::string, double>& params,
           Circuit::ModelCardHolder& holder) -> std::unique_ptr<Device> {
            auto* h = dynamic_cast<Circuit::TypedModelCardHolder<B4SOIModelCard>*>(&holder);
            if (!h) return nullptr;
            B4SOIDevice::Geom geom;
            if (auto it = params.find("w"); it != params.end()) geom.W = it->second;
            if (auto it = params.find("l"); it != params.end()) geom.L = it->second;
            return B4SOIDevice::make(std::string(name), nodes[0], nodes[1], nodes[2],
                                     nodes[3], GROUND_INTERNAL, GROUND_INTERNAL,
                                     geom, *h->card);
        }
    });
}

} // namespace neospice
