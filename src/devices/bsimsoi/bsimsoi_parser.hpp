#pragma once
#include "devices/bsimsoi/bsimsoi_device.hpp"
#include "devices/bsimsoi/bsimsoi_model_card.hpp"
#include "parser/model_cards.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace neospice {

// --- Model card cache and creation ---
inline std::unique_ptr<B4SOIModelCard> create_bsimsoi_model_card(
        const ModelCard& card) {
    return to_bsimsoi_card(card);
}

// --- Geometry fill helper ---
// Populate a B4SOIDevice::Geom from parsed element geometry.

// --- Device creation ---
// Use B4SOIDevice::make(name, n_d, n_g, n_s, n_e, n_p, n_b, geom, model_card)
// Terminals: d, g, s, e, p, b
// Geometry: W, L, M, AD, AS, PD, PS, NRD, NRS, SA, SB, SD, NF

} // namespace neospice
