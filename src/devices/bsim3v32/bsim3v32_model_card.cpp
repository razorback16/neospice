#include "devices/bsim3v32/bsim3v32_model_card.hpp"
#include "devices/bsim3v32/bsim3v32_def.hpp"
#include "devices/bsim3v32/bsim3v32_shim.hpp"
#include "devices/model_card_utils.hpp"

namespace neospice {

std::unique_ptr<BSIM3v32ModelCard> to_bsim3v32_card(const ModelCard& card) {
    auto out = std::make_unique<BSIM3v32ModelCard>();
    auto& ucb = out->ucb;

    constexpr ModelCardTypeEntry types[] = {{"nmos", 1}, {"pmos", -1}};
    ucb.BSIM3v32type = validate_model_type(card, types);
    ucb.BSIM3v32typeGiven = 1;

    namespace S = bsim3v32::Shim;
    convert_model_card_params<S::IfParm, S::IfValue>(
        card, ucb, bsim3v32::BSIM3v32mPTable, bsim3v32::BSIM3v32mPTSize,
        bsim3v32::BSIM3v32mParam, "BSIM3v32");

    return out;
}

} // namespace neospice
