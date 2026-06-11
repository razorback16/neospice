#include "devices/mos2/mos2_model_card.hpp"
#include "devices/mos2/mos2_def.hpp"
#include "devices/mos2/mos2_shim.hpp"
#include "devices/model_card_utils.hpp"

namespace neospice {

std::unique_ptr<MOS2ModelCard> to_mos2_card(const ModelCard& card) {
    auto out = std::make_unique<MOS2ModelCard>();
    auto& ucb = out->ucb;

    constexpr ModelCardTypeEntry types[] = {{"nmos", 1}, {"pmos", -1}};
    ucb.MOS2type = validate_model_type(card, types);
    ucb.MOS2typeGiven = 1;

    namespace S = mos2::Shim;
    convert_model_card_params<S::IfParm, S::IfValue>(
        card, ucb, mos2::MOS2mPTable, mos2::MOS2mPTSize,
        mos2::MOS2mParam, "MOS2");

    return out;
}

} // namespace neospice
