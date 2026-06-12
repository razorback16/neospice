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

/// Parse a `.func` definition from its token list (tokens[0] == ".func")
/// into the func_defs map. Joins tokens 1..end to reconstruct the signature
/// and body, so it works for both single-line and continuation forms.
void parse_func_def(const std::vector<std::string>& tokens,
                    std::unordered_map<std::string, FuncDef>& func_defs);

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

/// Substitute bare parameter identifiers in an expression string with their
/// numeric values, preserving expression structure. Identifiers followed by
/// '(' are treated as function calls and left alone; identifiers that are a
/// component of a dotted hierarchical name (adjacent to '.') are also left
/// intact (they are scoped device/node names, never params). Used to resolve
/// params inside behavioral E/G/B VALUE expressions while keeping arithmetic
/// (e.g. `{1.63m - IEE}` -> `{1.63m - 1e-05}`) foldable downstream.
std::string subst_param_names(
    const std::string& expr,
    const std::unordered_map<std::string, double>& params);

/// Resolve a list of (name, expression) parameter pairs in dependency order.
/// Handles forward references and detects circular dependencies.
/// Throws ParseError on circular dependencies or unknown parameter references.
std::unordered_map<std::string, double> resolve_params(
    const std::vector<std::pair<std::string, std::string>>& raw_params);

} // namespace neospice
