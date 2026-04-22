#pragma once
#include "devices/jfet2/jfet2_device.hpp"
#include "parser/model_cards.hpp"
#include <memory>

namespace neospice {

std::unique_ptr<JFET2ModelCard> to_jfet2_card(const ModelCard& card);

} // namespace neospice
