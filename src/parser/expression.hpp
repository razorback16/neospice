#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

namespace neospice {

/// A user-defined .func definition: name, formal parameters, and body.
struct FuncDef {
    std::vector<std::string> args;  // formal parameter names (lowercase)
    std::string body;               // expression body (without braces)
};

/// Expand user-defined function calls in an expression string.
/// Performs textual substitution: replaces `fname(arg1, arg2)` with the
/// function body, substituting formal parameters with actual arguments.
/// Handles nested calls (up to 10 expansion passes).
std::string expand_funcs(const std::string& expr,
                         const std::unordered_map<std::string, FuncDef>& func_defs);

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
