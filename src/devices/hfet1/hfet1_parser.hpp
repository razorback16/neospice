#pragma once
#include "devices/hfet1/hfet1_device.hpp"
#include "devices/hfet1/hfet1_model_card.hpp"
#include "parser/model_cards.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace neospice {

// --- Model card cache and creation ---
inline std::unique_ptr<HFETAModelCard> create_hfet1_model_card(
        const ModelCard& card) {
    return to_hfet1_card(card);
}

// --- Geometry fill helper ---
// Populate a HFETADevice::Geom from parsed element geometry.

// --- Device creation ---
// Use HFETADevice::make(name, n_drain, n_gate, n_source, geom, model_card)
// Terminals: drain, gate, source
// Geometry: L, W, M

} // namespace neospice
