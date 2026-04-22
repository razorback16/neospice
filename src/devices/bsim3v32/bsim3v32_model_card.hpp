#pragma once
#include "devices/bsim3v32/bsim3v32_device.hpp"
#include "parser/model_cards.hpp"
#include <memory>

namespace neospice {

std::unique_ptr<BSIM3v32ModelCard> to_bsim3v32_card(const ModelCard& card);

} // namespace neospice
