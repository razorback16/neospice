#pragma once
#include "devices/dio/dio_device.hpp"
#include "parser/model_cards.hpp"
#include <memory>

namespace neospice {

std::unique_ptr<DIOModelCard> to_dio_card(const ModelCard& card);

} // namespace neospice
