#pragma once
#include "parser/subcircuit.hpp"
#include "parser/tokenizer.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace neospice {

/// Maximum recursion depth for nested subcircuit expansion.
constexpr int MAX_SUBCIRCUIT_DEPTH = 100;

/// Expand a single X instance into flat element lines.
///
/// @param instance_prefix  Hierarchical name prefix (e.g., "x1" or "x1.xinv")
/// @param def              The subcircuit definition to expand
/// @param connections      Actual node names for each port
/// @param instance_params  Parameter overrides from X line (merged with defaults)
/// @param all_defs         All known subcircuit definitions (for recursive X expansion)
/// @param depth            Recursion depth (for infinite recursion detection)
/// @return Expanded element lines (no X lines, no .subckt/.ends, no .param)
std::vector<TokenizedLine> expand_instance(
    const std::string& instance_prefix,
    const SubcircuitDef& def,
    const std::vector<std::string>& connections,
    const std::unordered_map<std::string, double>& instance_params,
    const std::unordered_map<std::string, SubcircuitDef>& all_defs,
    int depth);

/// Expand all X instances in a list of tokenized lines.
/// Returns a new list with all X elements replaced by their expanded primitives.
/// Subcircuit definitions are looked up from all_defs plus any nested defs found
/// in subcircuit bodies.
std::vector<TokenizedLine> expand_all_instances(
    const std::vector<TokenizedLine>& lines,
    const std::unordered_map<std::string, SubcircuitDef>& all_defs);

} // namespace neospice
