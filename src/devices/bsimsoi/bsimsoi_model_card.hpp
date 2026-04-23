#pragma once
#include "devices/bsimsoi/bsimsoi_device.hpp"
#include "parser/model_cards.hpp"
#include <memory>

namespace neospice {

std::unique_ptr<B4SOIModelCard> to_bsimsoi_card(const ModelCard& card);

} // namespace neospice
