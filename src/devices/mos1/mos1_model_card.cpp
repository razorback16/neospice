#include "devices/mos1/mos1_model_card.hpp"
#include "devices/mos1/mos1_def.hpp"
#include "devices/mos1/mos1_shim.hpp"
#include "devices/model_card_utils.hpp"

namespace neospice {

std::unique_ptr<MOS1ModelCard> to_mos1_card(const ModelCard& card) {
    auto out = std::make_unique<MOS1ModelCard>();
    auto& ucb = out->ucb;

    constexpr ModelCardTypeEntry types[] = {{"nmos", 1}, {"pmos", -1}};
    ucb.MOS1type = validate_model_type(card, types);
    ucb.MOS1typeGiven = 1;

    namespace S = mos1::Shim;
    convert_model_card_params<S::IfParm, S::IfValue>(
        card, ucb, mos1::MOS1mPTable, mos1::MOS1mPTSize,
        mos1::MOS1mParam, "MOS1");

    return out;
}

} // namespace neospice
