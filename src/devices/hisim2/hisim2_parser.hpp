#pragma once
#include "devices/hisim2/hisim2_device.hpp"
#include "devices/hisim2/hisim2_model_card.hpp"
#include "parser/model_cards.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace neospice {

// --- Model card cache and creation ---
inline std::unique_ptr<HSM2ModelCard> create_hisim2_model_card(
        const ModelCard& card) {
    return to_hisim2_card(card);
}

// --- Geometry fill helper ---
// Populate a HSM2Device::Geom from parsed element geometry.

// --- Device creation ---
// Use HSM2Device::make(name, n_d, n_g, n_s, n_b, geom, model_card)
// Terminals: d, g, s, b
// Geometry: W, L, M, AD, AS, PD, PS, NRD, NRS, NF

} // namespace neospice
