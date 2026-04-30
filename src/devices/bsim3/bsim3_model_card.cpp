#include "devices/bsim3/bsim3_model_card.hpp"
#include "devices/bsim3/bsim3_def.hpp"
#include "devices/bsim3/bsim3_shim.hpp"
#include "devices/model_card_utils.hpp"

namespace neospice {

std::unique_ptr<BSIM3ModelCard> to_bsim3_card(const ModelCard& card) {
    auto out = std::make_unique<BSIM3ModelCard>();
    auto& ucb = out->ucb;

    constexpr ModelCardTypeEntry types[] = {{"nmos", 1}, {"pmos", -1}};
    ucb.BSIM3type = validate_model_type(card, types);
    ucb.BSIM3typeGiven = 1;

    namespace S = bsim3::Shim;
    convert_model_card_params<S::IfParm, S::IfValue>(
        card, ucb, bsim3::BSIM3mPTable, bsim3::BSIM3mPTSize,
        bsim3::BSIM3mParam, "BSIM3");

    return out;
}

} // namespace neospice
