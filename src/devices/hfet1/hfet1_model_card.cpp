#include "devices/hfet1/hfet1_model_card.hpp"
#include "devices/hfet1/hfet1_def.hpp"
#include "devices/hfet1/hfet1_shim.hpp"
#include "devices/model_card_utils.hpp"

namespace neospice {

std::unique_ptr<HFETAModelCard> to_hfet1_card(const ModelCard& card) {
    auto out = std::make_unique<HFETAModelCard>();
    auto& ucb = out->ucb;

    constexpr ModelCardTypeEntry types[] = {{"nhfet", 1}, {"phfet", -1}};
    ucb.HFETAtype = validate_model_type(card, types);

    namespace S = hfet1::Shim;
    convert_model_card_params<S::IfParm, S::IfValue>(
        card, ucb, hfet1::HFETAmPTable, hfet1::HFETAmPTSize,
        hfet1::HFETAmParam, "HFETA");

    return out;
}

} // namespace neospice
