#include "devices/hfet2/hfet2_model_card.hpp"
#include "devices/hfet2/hfet2_def.hpp"
#include "devices/hfet2/hfet2_shim.hpp"
#include "devices/model_card_utils.hpp"

namespace neospice {

std::unique_ptr<HFET2ModelCard> to_hfet2_card(const ModelCard& card) {
    auto out = std::make_unique<HFET2ModelCard>();
    auto& ucb = out->ucb;

    constexpr ModelCardTypeEntry types[] = {{"nhfet", 1}, {"phfet", -1}};
    ucb.HFET2type = validate_model_type(card, types);

    namespace S = hfet2::Shim;
    convert_model_card_params<S::IfParm, S::IfValue>(
        card, ucb, hfet2::HFET2mPTable, hfet2::HFET2mPTSize,
        hfet2::HFET2mParam, "HFET2");

    return out;
}

} // namespace neospice
