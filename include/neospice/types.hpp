#pragma once
#include <cstdint>
#include <functional>

namespace neospice {

enum class NodeId  : int32_t {};
enum class DevId   : int32_t {};
enum class ModelId : int32_t {};

inline constexpr NodeId GND{-1};

struct NodeIdHash {
    std::size_t operator()(NodeId id) const noexcept {
        return std::hash<int32_t>{}(static_cast<int32_t>(id));
    }
};
struct DevIdHash {
    std::size_t operator()(DevId id) const noexcept {
        return std::hash<int32_t>{}(static_cast<int32_t>(id));
    }
};
struct ModelIdHash {
    std::size_t operator()(ModelId id) const noexcept {
        return std::hash<int32_t>{}(static_cast<int32_t>(id));
    }
};

} // namespace neospice
