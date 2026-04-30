#include "dio_device.hpp"
#include "dio_model_card.hpp"
#include "devices/device_registry.hpp"

namespace neospice {

void register_dio(DeviceRegistry& reg) {
    // Model card factory
    reg.add_model_factory({
        "d", 0, 0,
        [](const ModelCard& card) -> std::unique_ptr<Circuit::ModelCardHolder> {
            return std::make_unique<Circuit::TypedModelCardHolder<DIOModelCard>>(
                to_dio_card(card), card.name, card.type);
        }
    });

    // Device builder
    reg.add_device_builder({
        'd', 0,
        [](std::string_view name,
           std::span<const int32_t> nodes,
           const std::unordered_map<std::string, double>& /*params*/,
           Circuit::ModelCardHolder& holder) -> std::unique_ptr<Device> {
            auto* h = dynamic_cast<Circuit::TypedModelCardHolder<DIOModelCard>*>(&holder);
            if (!h) return nullptr;
            DIODevice::Geom geom;
            return DIODevice::make(std::string(name), nodes[0], nodes[1],
                                   geom, *h->card);
        }
    });
}

} // namespace neospice
