#include "devices/hisimhv/hisimhv_model_card.hpp"
#include "devices/hisimhv/hisimhv_def.hpp"
#include "devices/hisimhv/hisimhv_shim.hpp"
#include "devices/model_card_utils.hpp"

namespace neospice {

std::unique_ptr<HSMHVModelCard> to_hisimhv_card(const ModelCard& card) {
    auto out = std::make_unique<HSMHVModelCard>();
    auto& ucb = out->ucb;

    constexpr ModelCardTypeEntry types[] = {{"nmos", 1}, {"pmos", -1}};
    ucb.HSMHV_type = validate_model_type(card, types);
    ucb.HSMHV_type_Given = 1;

    namespace S = hisimhv::Shim;
    convert_model_card_params<S::IfParm, S::IfValue>(
        card, ucb, hisimhv::HSMHVmPTable, hisimhv::HSMHVmPTSize,
        hisimhv::HSMHVmParam, "HSMHV");

    return out;
}

} // namespace neospice
