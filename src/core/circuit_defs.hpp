#pragma once
#include "core/circuit.hpp"
#include "parser/parse_state.hpp"

namespace neospice {

/// Complete definition of Circuit::DefinitionStore.
/// Must be visible in every TU that creates, destroys, or accesses
/// a unique_ptr<DefinitionStore> (circuit.cpp, circuit_include.cpp).
struct Circuit::DefinitionStore {
    DefinitionSet defs;
};

} // namespace neospice
