#pragma once
#include "devices/bjt/bjt_device.hpp"
#include "parser/model_cards.hpp"
#include <memory>

namespace neospice {

std::unique_ptr<BJTModelCard> to_bjt_card(const ModelCard& card);

} // namespace neospice
