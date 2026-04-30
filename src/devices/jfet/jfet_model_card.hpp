#pragma once
#include "devices/jfet/jfet_device.hpp"
#include "parser/model_cards.hpp"
#include <memory>

namespace neospice {

std::unique_ptr<JFETModelCard> to_jfet_card(const ModelCard& card);

} // namespace neospice
