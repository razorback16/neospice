#pragma once
#include "devices/bsim3v32/bsim3v32_device.hpp"
#include "devices/bsim3v32/bsim3v32_model_card.hpp"
#include "parser/model_cards.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace neospice {

// --- Model card cache and creation ---
inline std::unique_ptr<BSIM3v32ModelCard> create_bsim3v32_model_card(
        const ModelCard& card) {
    return to_bsim3v32_card(card);
}

// --- Geometry fill helper ---
// Populate a BSIM3v32Device::Geom from parsed element geometry.

// --- Device creation ---
// Use BSIM3v32Device::make(name, n_d, n_g, n_s, n_b, geom, model_card)
// Terminals: d, g, s, b
// Geometry: W, L, AD, AS, PD, PS, NRD, NRS, M

} // namespace neospice
