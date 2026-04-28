#pragma once
#include "devices/mes/mes_device.hpp"
#include "parser/model_cards.hpp"
#include <memory>

namespace neospice {

std::unique_ptr<MESModelCard> to_mes_card(const ModelCard& card);

} // namespace neospice
