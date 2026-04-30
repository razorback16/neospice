#include "hisim2_device.hpp"
#include "hisim2_model_card.hpp"
#include "devices/device_registry.hpp"

namespace neospice {

void register_hisim2(DeviceRegistry& reg) {
    // Model card factory — levels 61 and 68
    auto factory = [](const ModelCard& card) -> std::unique_ptr<Circuit::ModelCardHolder> {
        return std::make_unique<Circuit::TypedModelCardHolder<HSM2ModelCard>>(
            to_hisim2_card(card), card.name, card.type);
    };
    reg.add_model_factory({"mos", 61, 0, factory});
    reg.add_model_factory({"mos", 68, 0, factory});

    // Device builder — standard 4-node MOSFET pattern
    reg.add_device_builder({
        'm', 0,
        [](std::string_view name,
           std::span<const int32_t> nodes,
           const std::unordered_map<std::string, double>& params,
           Circuit::ModelCardHolder& holder) -> std::unique_ptr<Device> {
            auto* h = dynamic_cast<Circuit::TypedModelCardHolder<HSM2ModelCard>*>(&holder);
            if (!h) return nullptr;
            HSM2Device::Geom geom;
            if (auto it = params.find("w"); it != params.end()) geom.W = it->second;
            if (auto it = params.find("l"); it != params.end()) geom.L = it->second;
            return HSM2Device::make(std::string(name), nodes[0], nodes[1],
                                    nodes[2], nodes[3], geom, *h->card);
        }
    });
}

} // namespace neospice
