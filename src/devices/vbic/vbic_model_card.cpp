#include "devices/vbic/vbic_model_card.hpp"
#include "devices/vbic/vbic_def.hpp"
#include "devices/vbic/vbic_shim.hpp"
#include "devices/model_card_utils.hpp"

namespace neospice {

std::unique_ptr<VBICModelCard> to_vbic_card(const ModelCard& card) {
    auto out = std::make_unique<VBICModelCard>();
    auto& ucb = out->ucb;

    constexpr ModelCardTypeEntry types[] = {{"npn", 1}, {"pnp", -1}};
    ucb.VBICtype = validate_model_type(card, types);

    namespace S = vbic::Shim;
    convert_model_card_params<S::IfParm, S::IfValue>(
        card, ucb, vbic::VBICmPTable, vbic::VBICmPTSize,
        vbic::VBICmParam, "VBIC");

    return out;
}

} // namespace neospice
