#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

namespace neospice {

/// Evaluate a single arithmetic expression string.
/// The expression may be surrounded by braces: {expr} — braces are stripped.
double eval_expression(const std::string& expr,
                       const std::unordered_map<std::string, double>& params);

/// Resolve a list of (name, expression) parameter pairs in dependency order.
/// Handles forward references and detects circular dependencies.
/// Throws ParseError on circular dependencies or unknown parameter references.
std::unordered_map<std::string, double> resolve_params(
    const std::vector<std::pair<std::string, std::string>>& raw_params);

} // namespace neospice
