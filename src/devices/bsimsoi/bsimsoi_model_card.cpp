#include "devices/bsimsoi/bsimsoi_model_card.hpp"
#include "devices/bsimsoi/bsimsoi_def.hpp"
#include "devices/bsimsoi/bsimsoi_shim.hpp"
#include "devices/model_card_utils.hpp"

namespace neospice {

std::unique_ptr<B4SOIModelCard> to_bsimsoi_card(const ModelCard& card) {
    auto out = std::make_unique<B4SOIModelCard>();
    auto& ucb = out->ucb;

    constexpr ModelCardTypeEntry types[] = {{"nmos", 1}, {"pmos", -1}};
    ucb.B4SOItype = validate_model_type(card, types);
    ucb.B4SOItypeGiven = 1;

    namespace S = bsimsoi::Shim;
    convert_model_card_params<S::IfParm, S::IfValue>(
        card, ucb, bsimsoi::B4SOImPTable, bsimsoi::B4SOImPTSize,
        bsimsoi::B4SOImParam, "B4SOI");

    return out;
}

} // namespace neospice
