#include "devices/jfet/jfet_model_card.hpp"
#include "devices/jfet/jfet_def.hpp"
#include "devices/jfet/jfet_shim.hpp"
#include "devices/model_card_utils.hpp"

namespace neospice {

std::unique_ptr<JFETModelCard> to_jfet_card(const ModelCard& card) {
    auto out = std::make_unique<JFETModelCard>();
    auto& ucb = out->ucb;

    constexpr ModelCardTypeEntry types[] = {{"njf", 1}, {"pjf", -1}};
    ucb.JFETtype = validate_model_type(card, types);

    namespace S = jfet::Shim;
    convert_model_card_params<S::IfParm, S::IfValue>(
        card, ucb, jfet::JFETmPTable, jfet::JFETmPTSize,
        jfet::JFETmParam, "JFET");

    return out;
}

} // namespace neospice
