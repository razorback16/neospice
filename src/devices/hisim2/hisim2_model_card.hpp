#pragma once
#include "devices/hisim2/hisim2_device.hpp"
#include "parser/model_cards.hpp"
#include <memory>

namespace neospice {

std::unique_ptr<HSM2ModelCard> to_hisim2_card(const ModelCard& card);

} // namespace neospice
