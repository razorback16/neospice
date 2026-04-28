#pragma once
#include "devices/mes/mes_device.hpp"
#include "devices/mes/mes_model_card.hpp"
#include "parser/model_cards.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace neospice {

// --- Model card cache and creation ---
inline std::unique_ptr<MESModelCard> create_mes_model_card(
        const ModelCard& card) {
    return to_mes_card(card);
}

// --- Geometry fill helper ---
// Populate a MESDevice::Geom from parsed element geometry.

// --- Device creation ---
// Use MESDevice::make(name, n_drain, n_gate, n_source, geom, model_card)
// Terminals: drain, gate, source
// Geometry: area, m

} // namespace neospice
