#pragma once
#include "devices/vbic/vbic_device.hpp"
#include "parser/model_cards.hpp"
#include <memory>

namespace neospice {

std::unique_ptr<VBICModelCard> to_vbic_card(const ModelCard& card);

} // namespace neospice
