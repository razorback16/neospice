#pragma once
#include "devices/mos1/mos1_device.hpp"
#include "parser/model_cards.hpp"
#include <memory>

namespace neospice {

std::unique_ptr<MOS1ModelCard> to_mos1_card(const ModelCard& card);

} // namespace neospice
