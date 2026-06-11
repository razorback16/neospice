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

    // Pull out instance-default W/L before the model-param loop.
    // ngspice stores these in model->defaults and applies them to every
    // instance that didn't specify its own W/L (inpgmod.c:146-157, inpdpar.c:63-97).
    // W and L live in MOS1pTable (instance params), not MOS1mPTable (model params),
    // so convert_model_card_params() would emit "unknown parameter" warnings.
    // We intercept them here and store as model-level defaults instead.
    ModelCard filtered = card;  // copy so we can strip w/l
    if (auto it = filtered.params.find("w"); it != filtered.params.end()) {
        out->defaultW      = it->second;
        out->defaultWGiven = true;
        filtered.params.erase(it);
    }
    if (auto it = filtered.params.find("l"); it != filtered.params.end()) {
        out->defaultL      = it->second;
        out->defaultLGiven = true;
        filtered.params.erase(it);
    }

    namespace S = mos1::Shim;
    convert_model_card_params<S::IfParm, S::IfValue>(
        filtered, ucb, mos1::MOS1mPTable, mos1::MOS1mPTSize,
        mos1::MOS1mParam, "MOS1");

    return out;
}

} // namespace neospice
