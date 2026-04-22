#pragma once
#include "devices/mos3/mos3_device.hpp"
#include "parser/model_cards.hpp"
#include <memory>

namespace neospice {

std::unique_ptr<MOS3ModelCard> to_mos3_card(const ModelCard& card);

} // namespace neospice
