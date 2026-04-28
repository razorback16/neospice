#include "devices/mes/mes_model_card.hpp"
#include "devices/mes/mes_def.hpp"
#include "devices/mes/mes_shim.hpp"
#include "devices/model_card_utils.hpp"

namespace neospice {

std::unique_ptr<MESModelCard> to_mes_card(const ModelCard& card) {
    auto out = std::make_unique<MESModelCard>();
    auto& ucb = out->ucb;

    constexpr ModelCardTypeEntry types[] = {{"nmf", 1}, {"pmf", -1}};
    ucb.MEStype = validate_model_type(card, types);

    namespace S = mes::Shim;
    convert_model_card_params<S::IfParm, S::IfValue>(
        card, ucb, mes::MESmPTable, mes::MESmPTSize,
        mes::MESmParam, "MES");

    return out;
}

} // namespace neospice
