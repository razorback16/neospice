#include "bsim3_device.hpp"
#include "bsim3_model_card.hpp"
#include "devices/bsim3v32/bsim3v32_model_card.hpp"
#include "devices/device_registry.hpp"

namespace neospice {

void register_bsim3(DeviceRegistry& reg) {
    // Model card factory — levels 8 and 49 with VERSION gating
    auto factory = [](const ModelCard& card) -> std::unique_ptr<Circuit::ModelCardHolder> {
        auto ver_it = card.params.find("version");
        if (ver_it != card.params.end() && ver_it->second < 3.3)
            return std::make_unique<Circuit::TypedModelCardHolder<BSIM3v32ModelCard>>(
                to_bsim3v32_card(card), card.name, card.type);
        return std::make_unique<Circuit::TypedModelCardHolder<BSIM3ModelCard>>(
            to_bsim3_card(card), card.name, card.type);
    };
    reg.add_model_factory({"mos", 8, 0, factory});
    reg.add_model_factory({"mos", 49, 0, factory});

    // Device builder — only builds BSIM3 (not BSIM3v32)
    reg.add_device_builder({
        'm', 0,
        [](std::string_view name,
           std::span<const int32_t> nodes,
           const std::unordered_map<std::string, double>& params,
           Circuit::ModelCardHolder& holder) -> std::unique_ptr<Device> {
            auto* h = dynamic_cast<Circuit::TypedModelCardHolder<BSIM3ModelCard>*>(&holder);
            if (!h) return nullptr;
            BSIM3Device::Geom geom;
            if (auto it = params.find("w"); it != params.end()) geom.W = it->second;
            if (auto it = params.find("l"); it != params.end()) geom.L = it->second;
            return BSIM3Device::make(std::string(name), nodes[0], nodes[1],
                                     nodes[2], nodes[3], geom, *h->card);
        }
    });
}

} // namespace neospice
