#include "devices/mos3/mos3_model_card.hpp"
#include "devices/mos3/mos3_def.hpp"
#include "devices/mos3/mos3_shim.hpp"
#include "devices/model_card_utils.hpp"

namespace neospice {

std::unique_ptr<MOS3ModelCard> to_mos3_card(const ModelCard& card) {
    auto out = std::make_unique<MOS3ModelCard>();
    auto& ucb = out->ucb;

    constexpr ModelCardTypeEntry types[] = {{"nmos", 1}, {"pmos", -1}};
    ucb.MOS3type = validate_model_type(card, types);
    ucb.MOS3typeGiven = 1;

    namespace S = mos3::Shim;
    convert_model_card_params<S::IfParm, S::IfValue>(
        card, ucb, mos3::MOS3mPTable, mos3::MOS3mPTSize,
        mos3::MOS3mParam, "MOS3");

    return out;
}

} // namespace neospice
