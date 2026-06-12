#include "vdmos_device.hpp"
#include "vdmos_model_card.hpp"
#include "devices/device_registry.hpp"

namespace neospice {

void register_vdmos(DeviceRegistry& reg) {
    // Model card factory.  VDMOS model cards carry type "vdmos"/"vdmosn"/
    // "vdmosp"; the registry normalizes all three to the "vdmos" type group.
    reg.add_model_factory({
        "vdmos", 0, 0,
        [](const ModelCard& card) -> std::unique_ptr<Circuit::ModelCardHolder> {
            return std::make_unique<Circuit::TypedModelCardHolder<VDMOSModelCard>>(
                to_vdmos_card(card), card.name, card.type);
        }
    });
}

} // namespace neospice
