#pragma once
#include "devices/hisimhv/hisimhv_device.hpp"
#include "devices/hisimhv/hisimhv_model_card.hpp"
#include "parser/model_cards.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace neospice {

// --- Model card cache and creation ---
inline std::unique_ptr<HSMHVModelCard> create_hisimhv_model_card(
        const ModelCard& card) {
    return to_hisimhv_card(card);
}

// --- Geometry fill helper ---
// Populate a HSMHVDevice::Geom from parsed element geometry.

// --- Device creation ---
// Use HSMHVDevice::make(name, n_d, n_g, n_s, n_b, n_sub, geom, model_card)
// Terminals: d, g, s, b, sub
// Geometry: W, L, M, AD, AS, PD, PS, NRD, NRS, NF

} // namespace neospice
