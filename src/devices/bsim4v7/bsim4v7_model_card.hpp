#pragma once
#include "devices/bsim4v7/bsim4v7_device.hpp"
#include "parser/model_cards.hpp"
#include <memory>

namespace neospice {

std::unique_ptr<BSIM4v7ModelCard> to_bsim4_card(const ModelCard& card);

} // namespace neospice
