#pragma once
#include "devices/mos9/mos9_device.hpp"
#include "devices/mos9/mos9_model_card.hpp"
#include "parser/model_cards.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace neospice {

// --- Model card cache and creation ---
inline std::unique_ptr<MOS9ModelCard> create_mos9_model_card(
        const ModelCard& card) {
    return to_mos9_card(card);
}

// --- Geometry fill helper ---
// Populate a MOS9Device::Geom from parsed element geometry.

// --- Device creation ---
// Use MOS9Device::make(name, n_d, n_g, n_s, n_b, geom, model_card)
// Terminals: d, g, s, b
// Geometry: W, L, AD, AS, PD, PS, NRD, NRS, M

} // namespace neospice
