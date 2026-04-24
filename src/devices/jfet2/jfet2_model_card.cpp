#include "devices/jfet2/jfet2_model_card.hpp"
#include "devices/jfet2/jfet2_def.hpp"
#include "devices/jfet2/jfet2_shim.hpp"
#include "devices/model_card_utils.hpp"

namespace neospice {

std::unique_ptr<JFET2ModelCard> to_jfet2_card(const ModelCard& card) {
    auto out = std::make_unique<JFET2ModelCard>();
    auto& ucb = out->ucb;

    constexpr ModelCardTypeEntry types[] = {{"njf", 1}, {"pjf", -1}};
    ucb.JFET2type = validate_model_type(card, types);

    namespace S = jfet2::Shim;
    convert_model_card_params<S::IfParm, S::IfValue>(
        card, ucb, jfet2::JFET2mPTable, jfet2::JFET2mPTSize,
        jfet2::JFET2mParam, "JFET2");

    return out;
}

} // namespace neospice
