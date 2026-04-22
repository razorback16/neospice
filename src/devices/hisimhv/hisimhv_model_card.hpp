#pragma once
#include "devices/hisimhv/hisimhv_device.hpp"
#include "parser/model_cards.hpp"
#include <memory>

namespace neospice {

std::unique_ptr<HSMHVModelCard> to_hisimhv_card(const ModelCard& card);

} // namespace neospice
