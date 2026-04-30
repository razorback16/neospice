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

    return out;
}

} // namespace neospice
