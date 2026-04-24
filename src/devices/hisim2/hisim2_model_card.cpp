#include "devices/hisim2/hisim2_model_card.hpp"
#include "devices/hisim2/hisim2_def.hpp"
#include "devices/hisim2/hisim2_shim.hpp"
#include "devices/model_card_utils.hpp"

namespace neospice {

std::unique_ptr<HSM2ModelCard> to_hisim2_card(const ModelCard& card) {
    auto out = std::make_unique<HSM2ModelCard>();
    auto& ucb = out->ucb;

    constexpr ModelCardTypeEntry types[] = {{"nmos", 1}, {"pmos", -1}};
    ucb.HSM2_type = validate_model_type(card, types);
    ucb.HSM2_type_Given = 1;

    namespace S = hisim2::Shim;
    convert_model_card_params<S::IfParm, S::IfValue>(
        card, ucb, hisim2::HSM2mPTable, hisim2::HSM2mPTSize,
        hisim2::HSM2mParam, "HSM2");

    return out;
}

} // namespace neospice
