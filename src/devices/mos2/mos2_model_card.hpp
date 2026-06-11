#pragma once
#include "devices/mos2/mos2_device.hpp"
#include "parser/model_cards.hpp"
#include <memory>

namespace neospice {

std::unique_ptr<MOS2ModelCard> to_mos2_card(const ModelCard& card);

} // namespace neospice
