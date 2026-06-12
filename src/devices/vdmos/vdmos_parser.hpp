#pragma once
#include "devices/vdmos/vdmos_device.hpp"
#include "devices/vdmos/vdmos_model_card.hpp"
#include "parser/model_cards.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace neospice {

// --- Model card cache and creation ---
inline std::unique_ptr<VDMOSModelCard> create_vdmos_model_card(
        const ModelCard& card) {
    return to_vdmos_card(card);
}

// --- Geometry fill helper ---
// Populate a VDMOSDevice::Geom from parsed element geometry.

// --- Device creation ---
// Use VDMOSDevice::make(name, n_d, n_g, n_s, n_tj, n_tc, geom, model_card)
// Terminals: d, g, s, tj, tc
// Geometry: M

} // namespace neospice
