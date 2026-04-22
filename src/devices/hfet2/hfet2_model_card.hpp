#pragma once
#include "devices/hfet2/hfet2_device.hpp"
#include "parser/model_cards.hpp"
#include <memory>

namespace neospice {

std::unique_ptr<HFET2ModelCard> to_hfet2_card(const ModelCard& card);

} // namespace neospice
