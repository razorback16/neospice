#include "devices/bsim4v7/bsim4v7_model_card.hpp"
#include "devices/bsim4v7/bsim4v7_def.hpp"
#include "devices/bsim4v7/bsim4v7_shim.hpp"
#include "devices/model_card_utils.hpp"

namespace neospice {

std::unique_ptr<BSIM4v7ModelCard> to_bsim4_card(const ModelCard& card) {
    auto out = std::make_unique<BSIM4v7ModelCard>();
    auto& ucb = out->ucb;

    constexpr ModelCardTypeEntry types[] = {{"nmos", 1}, {"pmos", -1}};
    ucb.BSIM4v7type = validate_model_type(card, types);
    ucb.BSIM4v7typeGiven = 1;

    namespace S = bsim4v7::Shim;
    convert_model_card_params<S::IfParm, S::IfValue>(
        card, ucb, bsim4v7::BSIM4v7mPTable, bsim4v7::BSIM4v7mPTSize,
        bsim4v7::BSIM4v7mParam, "BSIM4v7");

    return out;
}

} // namespace neospice
