#pragma once
#include "devices/jfet2/jfet2_device.hpp"
#include "devices/jfet2/jfet2_model_card.hpp"
#include "parser/model_cards.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace neospice {

// --- Model card cache and creation ---
inline std::unique_ptr<JFET2ModelCard> create_jfet2_model_card(
        const ModelCard& card) {
    return to_jfet2_card(card);
}

// --- Geometry fill helper ---
// Populate a JFET2Device::Geom from parsed element geometry.

// --- Device creation ---
// Use JFET2Device::make(name, n_drain, n_gate, n_source, geom, model_card)
// Terminals: drain, gate, source
// Geometry: area, m

} // namespace neospice
