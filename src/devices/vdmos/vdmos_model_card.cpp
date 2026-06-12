#include "devices/vdmos/vdmos_model_card.hpp"
#include "devices/vdmos/vdmos_def.hpp"
#include "devices/vdmos/vdmos_shim.hpp"
#include "devices/model_card_utils.hpp"

namespace neospice {

std::unique_ptr<VDMOSModelCard> to_vdmos_card(const ModelCard& card) {
    auto out = std::make_unique<VDMOSModelCard>();
    auto& ucb = out->ucb;

    constexpr ModelCardTypeEntry types[] = {{"vdmos", 1}, {"vdmosn", 1}, {"vdmosp", -1}};
    ucb.VDMOStype = validate_model_type(card, types);
    ucb.VDMOStypeGiven = 1;

    namespace S = vdmos::Shim;
    convert_model_card_params<S::IfParm, S::IfValue>(
        card, ucb, vdmos::VDMOSmPTable, vdmos::VDMOSmPTSize,
        vdmos::VDMOSmParam, "VDMOS");

    return out;
}

} // namespace neospice
