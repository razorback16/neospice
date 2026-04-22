#pragma once
#include "devices/mos3/mos3_device.hpp"
#include "devices/mos3/mos3_model_card.hpp"
#include "parser/model_cards.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace neospice {

// --- Model card cache and creation ---
inline std::unique_ptr<MOS3ModelCard> create_mos3_model_card(
        const ModelCard& card) {
    return to_mos3_card(card);
}

// --- Geometry fill helper ---
// Populate a MOS3Device::Geom from parsed element geometry.

// --- Device creation ---
// Use MOS3Device::make(name, n_d, n_g, n_s, n_b, geom, model_card)
// Terminals: d, g, s, b
// Geometry: W, L, AD, AS, PD, PS, NRD, NRS, M

} // namespace neospice
