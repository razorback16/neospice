#pragma once
#include "devices/bsim3/bsim3_device.hpp"
#include "parser/model_cards.hpp"
#include <memory>

namespace neospice {

std::unique_ptr<BSIM3ModelCard> to_bsim3_card(const ModelCard& card);

} // namespace neospice
