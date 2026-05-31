#include "devices/bjt/bjt_model_card.hpp"
#include "devices/bjt/bjt_def.hpp"
#include "devices/bjt/bjt_shim.hpp"
#include "devices/model_card_utils.hpp"

namespace neospice {

std::unique_ptr<BJTModelCard> to_bjt_card(const ModelCard& card) {
    auto out = std::make_unique<BJTModelCard>();
    auto& ucb = out->ucb;

    // LPNP (lateral PNP) maps to PNP polarity; ngspice rewrites it to
    // "PNP ... subs=-1" (inpcompat.c:789), and neospice's setup already
    // defaults any PNP to a LATERAL substrate, so no extra handling is needed.
    constexpr ModelCardTypeEntry types[] = {{"npn", 1}, {"pnp", -1}, {"lpnp", -1}};
    ucb.BJTtype = validate_model_type(card, types);

    namespace S = bjt::Shim;
    convert_model_card_params<S::IfParm, S::IfValue>(
        card, ucb, bjt::BJTmPTable, bjt::BJTmPTSize,
        bjt::BJTmParam, "BJT");

    return out;
}

} // namespace neospice
