#pragma once
#include "devices/hfet2/hfet2_device.hpp"
#include "devices/hfet2/hfet2_model_card.hpp"
#include "parser/model_cards.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace neospice {

// --- Model card cache and creation ---
inline std::unique_ptr<HFET2ModelCard> create_hfet2_model_card(
        const ModelCard& card) {
    return to_hfet2_card(card);
}

// --- Geometry fill helper ---
// Populate a HFET2Device::Geom from parsed element geometry.

// --- Device creation ---
// Use HFET2Device::make(name, n_drain, n_gate, n_source, geom, model_card)
// Terminals: drain, gate, source
// Geometry: L, W, M

} // namespace neospice
