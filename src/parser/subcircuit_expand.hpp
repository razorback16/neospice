#pragma once
#include "parser/subcircuit.hpp"
#include "parser/tokenizer.hpp"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace neospice {

/// Maximum recursion depth for nested subcircuit expansion.
constexpr int MAX_SUBCIRCUIT_DEPTH = 100;

/// Expand all X instances in a list of tokenized lines.
/// Returns a new list with all X elements replaced by their expanded primitives.
/// Subcircuit definitions are looked up from all_defs plus any nested defs found
/// in subcircuit bodies.
///
/// @param global_params  Top-level .param values resolved before expansion;
///                       used as a base for parameter evaluation in instances.
/// @param global_nodes   Node names declared via .global that should never be
///                       prefixed during subcircuit expansion (case-insensitive,
///                       stored as lowercase).
std::vector<TokenizedLine> expand_all_instances(
    const std::vector<TokenizedLine>& lines,
    const std::unordered_map<std::string, SubcircuitDef>& all_defs,
    const std::unordered_map<std::string, double>& global_params = {},
    const std::unordered_set<std::string>& global_nodes = {});

} // namespace neospice
