#pragma once

#include "devices/device.hpp"   // GROUND_INTERNAL

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

namespace neospice {

/// Convert a neospice node index (GROUND_INTERNAL = -1 for ground,
/// non-negative for real nodes) to the UCB convention (0 = ground, >= 1).
inline int neo_to_ucb(int32_t neo) {
    return (neo < 0) ? 0 : (neo + 1);
}

/// Inverse of neo_to_ucb: convert a UCB node index back to neospice convention.
inline int32_t ucb_to_neo(int ucb_node) {
    return (ucb_node <= 0) ? GROUND_INTERNAL : (ucb_node - 1);
}

/// Return a lowercased copy of @p s.
inline std::string str_tolower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

} // namespace neospice
