#include "devices/dio/dio_model_card.hpp"
#include "devices/dio/dio_def.hpp"
#include "devices/dio/dio_shim.hpp"
#include "devices/model_card_utils.hpp"

namespace neospice {

std::unique_ptr<DIOModelCard> to_dio_card(const ModelCard& card) {
    auto out = std::make_unique<DIOModelCard>();
    auto& ucb = out->ucb;

    namespace S = dio::Shim;
    convert_model_card_params<S::IfParm, S::IfValue>(
        card, ucb, dio::DIOmPTable, dio::DIOmPTSize,
        dio::DIOmParam, "DIO");

    // PSpice per-device temperature (ngspice rewrites these to temp/tnom on the
    // model card, inpcompat.c:1014): T_ABS forces the operating temperature of
    // all instances; T_MEASURED is the parameter-measurement temperature (TNOM).
    if (card.t_abs) {
        ucb.DIOtempModel = *card.t_abs + 273.15;
        ucb.DIOtempModelGiven = 1;
    }
    if (card.t_measured) {
        ucb.DIOnomTemp = *card.t_measured + 273.15;
        ucb.DIOnomTempGiven = 1;
    }

    return out;
}

} // namespace neospice
