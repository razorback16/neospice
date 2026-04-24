#include "devices/mos9/mos9_model_card.hpp"
#include "devices/mos9/mos9_def.hpp"
#include "devices/mos9/mos9_shim.hpp"
#include "devices/model_card_utils.hpp"

namespace neospice {

std::unique_ptr<MOS9ModelCard> to_mos9_card(const ModelCard& card) {
    auto out = std::make_unique<MOS9ModelCard>();
    auto& ucb = out->ucb;

    constexpr ModelCardTypeEntry types[] = {{"nmos", 1}, {"pmos", -1}};
    ucb.MOS9type = validate_model_type(card, types);
    ucb.MOS9typeGiven = 1;

    namespace S = mos9::Shim;
    convert_model_card_params<S::IfParm, S::IfValue>(
        card, ucb, mos9::MOS9mPTable, mos9::MOS9mPTSize,
        mos9::MOS9mParam, "MOS9");

    return out;
}

} // namespace neospice
