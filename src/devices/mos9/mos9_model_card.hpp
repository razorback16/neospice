#pragma once
#include "devices/mos9/mos9_device.hpp"
#include "parser/model_cards.hpp"
#include <memory>

namespace neospice {

std::unique_ptr<MOS9ModelCard> to_mos9_card(const ModelCard& card);

} // namespace neospice
