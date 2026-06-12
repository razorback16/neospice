#pragma once
#include "devices/vdmos/vdmos_device.hpp"
#include "parser/model_cards.hpp"
#include <memory>

namespace neospice {

std::unique_ptr<VDMOSModelCard> to_vdmos_card(const ModelCard& card);

} // namespace neospice
