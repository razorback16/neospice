#pragma once
#include "devices/hfet1/hfet1_device.hpp"
#include "parser/model_cards.hpp"
#include <memory>

namespace neospice {

std::unique_ptr<HFETAModelCard> to_hfet1_card(const ModelCard& card);

} // namespace neospice
