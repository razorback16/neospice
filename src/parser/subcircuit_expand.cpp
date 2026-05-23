#include "parser/subcircuit_expand.hpp"
#include "parser/expression.hpp"
#include "core/types.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <unordered_set>

namespace neospice {

namespace {

std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

/// Check whether a node name is global (ground or declared via .global).
/// Global nodes are never substituted during subcircuit expansion.
bool is_global_node(const std::string& name,
                    const std::unordered_set<std::string>& global_nodes) {
    std::string lower = to_lower(name);
    return lower == "0" || lower == "gnd" || global_nodes.count(lower) > 0;
}

/// Determine the number of node tokens for an element line based on the
/// first character of the device name.
/// Returns the number of tokens after the device name that are node names.
/// For X elements, this is determined by looking up the subcircuit port count.
/// Returns 0 for unknown element types.
int node_count_for_element(char elem_type) {
    switch (elem_type) {
        case 'r': case 'c': case 'l':
        case 'v': case 'i':
        case 'd':
            return 2;
        case 'm':
        case 'e': case 'g':
            return 4;
        case 'b':
            return 2;
        case 'h': case 'f':
            return 2;
        case 'q':
            return 4;  // Q has 4 node tokens (C B E S) — substrate should be explicit in subcircuits
        case 'j':
            return 3;  // J has 3 node tokens (D G S)
        case 'z':
            return 3;  // Z (MESFET/HFET) has 3 node tokens (D G S)
        case 't': case 'o':
            return 4;  // T (transmission line) and O (LTRA) have 4 node tokens
        case 's':
            return 4;  // S (voltage switch): n+ n- nc+ nc-
        case 'w':
            return 2;  // W (current switch): n+ n- (then Vname model)
        case 'k':
            return 0;  // K has no node tokens (references inductor device names, not nodes)
        default:
            return 0;
    }
}

/// Replace every {expr} in a string with its evaluated numeric value,
/// preserving all surrounding text (parens, punctuation, etc.).
std::string subst_brace_params(
    const std::string& token,
    const std::unordered_map<std::string, double>& params) {
    std::string result;
    result.reserve(token.size());
    size_t i = 0;
    while (i < token.size()) {
        if (token[i] == '{') {
            size_t close = token.find('}', i + 1);
            if (close == std::string::npos) {
                result += token.substr(i);
                break;
            }
            std::string expr = token.substr(i, close - i + 1); // includes braces
            try {
                double val = eval_expression(expr, params);
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.15g", val);
                result += buf;
            } catch (...) {
                result += expr;
            }
            i = close + 1;
        } else {
            result += token[i];
            ++i;
        }
    }
    return result;
}

/// Check if a token looks like a parameter expression (contains braces or
/// references a known parameter name).
/// Substitute node references inside an ABM expression string.
/// Finds V(name), V(name,name), and I(Vname) patterns and applies subst_node
/// to each node/device name.  Also substitutes bare parameter names.
std::string subst_expr_nodes(
    const std::string& expr,
    const std::function<std::string(const std::string&)>& subst_node,
    const std::string& instance_prefix) {

    std::string result;
    result.reserve(expr.size() * 2);
    size_t i = 0;
    size_t len = expr.size();

    while (i < len) {
        // Look for V( or I( patterns (case-insensitive)
        if (i + 1 < len &&
            (std::tolower(static_cast<unsigned char>(expr[i])) == 'v' ||
             std::tolower(static_cast<unsigned char>(expr[i])) == 'i') &&
            expr[i + 1] == '(') {

            char func_char = expr[i];
            bool is_current = (std::tolower(static_cast<unsigned char>(func_char)) == 'i');

            // Make sure this is a standalone function call, not part of a longer
            // identifier like "VMID" or "IF".  The preceding character (if any)
            // must be non-alphanumeric and non-underscore.
            if (i > 0) {
                char prev = expr[i - 1];
                if (std::isalnum(static_cast<unsigned char>(prev)) || prev == '_') {
                    result += expr[i];
                    ++i;
                    continue;
                }
            }

            // Find the matching close paren
            size_t paren_start = i + 2;
            int depth = 1;
            size_t j = paren_start;
            while (j < len && depth > 0) {
                if (expr[j] == '(') ++depth;
                else if (expr[j] == ')') --depth;
                ++j;
            }
            if (depth != 0) {
                // Unmatched paren — pass through
                result += expr[i];
                ++i;
                continue;
            }
            // j now points past the closing ')'
            std::string inner = expr.substr(paren_start, j - 1 - paren_start);

            if (is_current) {
                // I(Vname) — prefix the device name
                std::string dev_lower = to_lower(inner);
                std::string subst = instance_prefix + "." + dev_lower;
                result += func_char;
                result += '(';
                result += subst;
                result += ')';
            } else {
                // V(name) or V(name,name)
                size_t comma = inner.find(',');
                if (comma != std::string::npos) {
                    std::string n1 = inner.substr(0, comma);
                    std::string n2 = inner.substr(comma + 1);
                    // Trim whitespace
                    while (!n1.empty() && std::isspace(static_cast<unsigned char>(n1.back()))) n1.pop_back();
                    while (!n2.empty() && std::isspace(static_cast<unsigned char>(n2.front()))) n2.erase(0, 1);
                    result += func_char;
                    result += '(';
                    result += subst_node(n1);
                    result += ',';
                    result += subst_node(n2);
                    result += ')';
                } else {
                    result += func_char;
                    result += '(';
                    result += subst_node(inner);
                    result += ')';
                }
            }
            i = j;
        } else {
            result += expr[i];
            ++i;
        }
    }
    return result;
}

bool is_param_expr(const std::string& token,
                   const std::unordered_map<std::string, double>& params) {
    if (token.find('{') != std::string::npos) return true;
    // Check if the entire token is a parameter name
    std::string lower = to_lower(token);
    return params.find(lower) != params.end();
}

/// Try to evaluate a value token. If it contains a parameter reference or
/// braced expression, evaluate it. Otherwise return the token as-is.
std::string eval_value_token(const std::string& token,
                             const std::unordered_map<std::string, double>& params) {
    if (!is_param_expr(token, params)) {
        return token;
    }
    try {
        double val = eval_expression(token, params);
        // Format without trailing zeros but with enough precision
        // Use scientific notation for very small/large values
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.15g", val);
        return std::string(buf);
    } catch (...) {
        // If evaluation fails, return token unchanged
        return token;
    }
}

/// Extract subcircuit definitions from a body (handles nested .subckt/.ends).
/// Returns the definitions and the remaining lines (with .subckt/.ends removed).
void extract_nested_defs(
    const std::vector<TokenizedLine>& body,
    std::unordered_map<std::string, SubcircuitDef>& nested_defs,
    std::vector<TokenizedLine>& remaining) {

    int depth = 0;
    SubcircuitDef current_def;
    std::vector<SubcircuitDef> def_stack;

    for (const auto& line : body) {
        if (line.tokens.empty()) {
            if (depth > 0) {
                current_def.body.push_back(line);
            } else {
                remaining.push_back(line);
            }
            continue;
        }

        std::string first = to_lower(line.tokens[0]);

        if (first == ".subckt") {
            if (depth > 0) {
                // Nested .subckt — push current onto stack
                def_stack.push_back(std::move(current_def));
                current_def = SubcircuitDef{};
            }

            current_def.name = to_lower(line.tokens[1]);
            current_def.ports.clear();
            current_def.default_params.clear();
            current_def.body.clear();
            current_def.source_line = line.line_number;

            bool seen_param = false;
            for (size_t i = 2; i < line.tokens.size(); ++i) {
                const std::string& tok = line.tokens[i];
                // PSpice section keywords — skip the keyword itself
                std::string tok_lower = to_lower(tok);
                if (tok_lower == "params:" || tok_lower == "optional:" || tok_lower == "text:") {
                    if (tok_lower == "params:") seen_param = true;
                    continue;
                }
                auto eq_pos = tok.find('=');
                if (eq_pos != std::string::npos) {
                    std::string key = to_lower(tok.substr(0, eq_pos));
                    std::string val = tok.substr(eq_pos + 1);
                    if (val.empty() && i + 1 < line.tokens.size() &&
                        line.tokens[i + 1].find('=') == std::string::npos &&
                        to_lower(line.tokens[i + 1]) != "params:" &&
                        to_lower(line.tokens[i + 1]) != "optional:") {
                        val = line.tokens[++i];
                    }
                    current_def.default_params.emplace_back(key, val);
                    seen_param = true;
                } else if (seen_param && i + 1 < line.tokens.size() &&
                           line.tokens[i + 1] == "=" && i + 2 < line.tokens.size()) {
                    std::string key = to_lower(tok);
                    std::string val = line.tokens[i + 2];
                    current_def.default_params.emplace_back(key, val);
                    i += 2;
                } else {
                    current_def.ports.push_back(to_lower(tok));
                }
            }
            depth++;

        } else if (first == ".ends") {
            if (depth == 0) {
                // Shouldn't happen in a well-formed body, but pass through
                remaining.push_back(line);
                continue;
            }

            depth--;

            if (depth == 0) {
                nested_defs[current_def.name] = std::move(current_def);
                current_def = SubcircuitDef{};
            } else {
                // End of a doubly-nested subcircuit
                SubcircuitDef inner_def = std::move(current_def);
                current_def = std::move(def_stack.back());
                def_stack.pop_back();

                // Reconstruct .subckt header for the inner def
                TokenizedLine subckt_hdr;
                subckt_hdr.line_number = inner_def.source_line;
                subckt_hdr.tokens.push_back(".subckt");
                subckt_hdr.tokens.push_back(inner_def.name);
                for (const auto& port : inner_def.ports) {
                    subckt_hdr.tokens.push_back(port);
                }
                for (const auto& [key, val] : inner_def.default_params) {
                    subckt_hdr.tokens.push_back(key + "=" + val);
                }
                current_def.body.push_back(subckt_hdr);
                for (const auto& bl : inner_def.body) {
                    current_def.body.push_back(bl);
                }
                current_def.body.push_back(line);
            }
        } else {
            if (depth > 0) {
                current_def.body.push_back(line);
            } else {
                remaining.push_back(line);
            }
        }
    }
}

/// Collect all node names that appear in element lines of the body.
/// Returns the set of internal node names (not ports, not ground, not .global).
std::unordered_set<std::string> collect_internal_nodes(
    const std::vector<TokenizedLine>& body,
    const std::unordered_set<std::string>& port_set,
    const std::unordered_map<std::string, SubcircuitDef>& all_defs,
    const std::unordered_set<std::string>& global_nodes) {

    std::unordered_set<std::string> internal_nodes;

    for (const auto& line : body) {
        if (line.tokens.empty()) continue;
        std::string first = to_lower(line.tokens[0]);

        // Skip dot commands
        if (first[0] == '.') continue;

        char elem_type = std::tolower(static_cast<unsigned char>(first[0]));

        // Determine which token positions are nodes
        std::vector<size_t> node_positions;

        if (elem_type == 'x') {
            // X instance: Xname n1 n2 ... subckt_name [key=val ...]
            // Find subcircuit name: last token without '=' that's a known subcircuit
            std::string subckt_name;
            size_t subckt_pos = 0;
            for (size_t i = line.tokens.size(); i > 1; --i) {
                const std::string& tok = line.tokens[i - 1];
                if (tok.find('=') != std::string::npos) continue;
                std::string lower_tok = to_lower(tok);
                if (all_defs.find(lower_tok) != all_defs.end()) {
                    subckt_name = lower_tok;
                    subckt_pos = i - 1;
                    break;
                }
            }
            if (!subckt_name.empty()) {
                // Nodes are tokens[1] through tokens[subckt_pos-1]
                for (size_t i = 1; i < subckt_pos; ++i) {
                    node_positions.push_back(i);
                }
            }
        } else {
            int ncount = node_count_for_element(elem_type);

            // E/G POLY: only 2 output nodes, then POLY(N) + 2*N control nodes
            if ((elem_type == 'e' || elem_type == 'g') &&
                line.tokens.size() > 3 &&
                to_lower(line.tokens[3]).substr(0, 4) == "poly") {
                ncount = 2;  // output nodes only
                std::string pt = to_lower(line.tokens[3]);
                int pdim = 1;
                size_t paren = pt.find('(');
                if (paren != std::string::npos) {
                    size_t close = pt.find(')');
                    if (close != std::string::npos && close > paren)
                        pdim = std::stoi(pt.substr(paren + 1, close - paren - 1));
                }
                // Skip POLY token (position 3), then 2*N control nodes
                size_t ctrl_start = 4;
                if (ctrl_start < line.tokens.size() && line.tokens[ctrl_start].front() == '(')
                    ctrl_start++;  // skip split "(N)" token
                for (int k = 0; k < 2 * pdim; ++k) {
                    node_positions.push_back(ctrl_start + k);
                }
            }

            for (int i = 0; i < ncount; ++i) {
                node_positions.push_back(static_cast<size_t>(1 + i));
            }
        }

        // Check each node position
        for (size_t pos : node_positions) {
            if (pos >= line.tokens.size()) continue;
            std::string node = to_lower(line.tokens[pos]);
            if (!is_global_node(node, global_nodes) && port_set.find(node) == port_set.end()) {
                internal_nodes.insert(node);
            }
        }
    }

    return internal_nodes;
}

/// Expand a single X instance into flat element lines (internal helper).
///
/// @param instance_prefix  Hierarchical name prefix (e.g., "x1" or "x1.xinv")
/// @param def              The subcircuit definition to expand
/// @param connections      Actual node names for each port
/// @param instance_params  Parameter overrides from X line (merged with defaults)
/// @param all_defs         All known subcircuit definitions (for recursive X expansion)
/// @param depth            Recursion depth (for infinite recursion detection)
/// @param line_number      Source line number of the X element (for error messages)
/// @param global_params    Top-level .param values (base for parameter resolution)
/// @param global_nodes     Node names declared via .global (never prefixed)
std::vector<TokenizedLine> expand_instance(
    const std::string& instance_prefix,
    const SubcircuitDef& def,
    const std::vector<std::string>& connections,
    const std::unordered_map<std::string, double>& instance_params,
    const std::unordered_map<std::string, SubcircuitDef>& all_defs,
    int depth,
    int line_number,
    const std::unordered_map<std::string, double>& global_params,
    const std::unordered_set<std::string>& global_nodes) {

    if (depth > MAX_SUBCIRCUIT_DEPTH) {
        throw ParseError("Line " + std::to_string(line_number) +
                         ": Maximum subcircuit nesting depth (" +
                         std::to_string(MAX_SUBCIRCUIT_DEPTH) +
                         ") exceeded — possible infinite recursion in subcircuit '" +
                         def.name + "'");
    }

    // Verify port count matches — use a local copy so we can truncate
    std::vector<std::string> conns = connections;
    if (conns.size() != def.ports.size()) {
        if (conns.size() > def.ports.size()) {
            fprintf(stderr, "Warning: Line %d: Subcircuit '%s' expects %zu port(s) but got %zu connection(s) — truncating\n",
                    line_number, def.name.c_str(), def.ports.size(), conns.size());
            conns.resize(def.ports.size());
        } else {
            fprintf(stderr, "Warning: Line %d: Subcircuit '%s' expects %zu port(s) but got %zu connection(s) — skipping\n",
                    line_number, def.name.c_str(), def.ports.size(), conns.size());
            return {};
        }
    }

    // Build port-to-connection map
    std::unordered_map<std::string, std::string> node_map;
    for (size_t i = 0; i < def.ports.size(); ++i) {
        node_map[def.ports[i]] = conns[i];
    }

    // Extract nested subcircuit definitions from the body
    std::unordered_map<std::string, SubcircuitDef> nested_defs;
    std::vector<TokenizedLine> body_lines;
    extract_nested_defs(def.body, nested_defs, body_lines);

    // Merge nested defs with all_defs (nested take precedence locally)
    auto merged_defs = all_defs;
    for (auto& [name, ndef] : nested_defs) {
        merged_defs[name] = ndef;
    }

    // Build port set for internal node detection
    std::unordered_set<std::string> port_set(def.ports.begin(), def.ports.end());

    // Collect internal nodes from body
    auto internal_nodes = collect_internal_nodes(body_lines, port_set, merged_defs, global_nodes);

    // Map internal nodes to prefixed names
    for (const auto& inode : internal_nodes) {
        node_map[inode] = instance_prefix + "." + inode;
    }

    // Build merged parameter map: defaults + overrides
    // Start with subcircuit defaults, then apply instance overrides
    std::vector<std::pair<std::string, std::string>> raw_params;
    for (const auto& [key, expr] : def.default_params) {
        raw_params.emplace_back(key, expr);
    }
    // Override with instance params (converted back to string for resolve_params)
    for (const auto& [key, val] : instance_params) {
        // Check if this overrides a default — replace it
        bool found = false;
        for (auto& [rkey, rval] : raw_params) {
            if (rkey == key) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.15g", val);
                rval = std::string(buf);
                found = true;
                break;
            }
        }
        if (!found) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.15g", val);
            raw_params.emplace_back(key, std::string(buf));
        }
    }

    // Resolve parameters — start from global_params as a base so that
    // subcircuit expressions can reference top-level .param values.
    std::unordered_map<std::string, double> params = global_params;
    if (!raw_params.empty()) {
        auto resolved = resolve_params(raw_params);
        for (auto& [k, v] : resolved) {
            params[k] = v;
        }
    }

    // Helper: substitute a node name using the node_map
    auto subst_node = [&](const std::string& name) -> std::string {
        std::string lower = to_lower(name);
        if (is_global_node(lower, global_nodes)) return name;  // global node — never prefix
        auto it = node_map.find(lower);
        if (it != node_map.end()) return it->second;
        // Not a known port or internal node — treat as internal (prefix)
        return instance_prefix + "." + lower;
    };

    // Pre-collect .model names from the subcircuit body so we can
    // distinguish model references from node names on Q/J cards.
    std::unordered_set<std::string> local_model_names;
    for (const auto& line : body_lines) {
        if (line.tokens.size() >= 2 && to_lower(line.tokens[0]) == ".model") {
            local_model_names.insert(to_lower(line.tokens[1]));
        }
    }

    std::vector<TokenizedLine> result;

    for (const auto& line : body_lines) {
        if (line.tokens.empty()) continue;
        std::string first = to_lower(line.tokens[0]);

        // Skip .param lines — already resolved into params map
        if (first == ".param") {
            // But add local .param definitions to params map
            for (size_t i = 1; i < line.tokens.size(); ++i) {
                auto eq_pos = line.tokens[i].find('=');
                if (eq_pos != std::string::npos) {
                    std::string key = to_lower(line.tokens[i].substr(0, eq_pos));
                    std::string val_str = line.tokens[i].substr(eq_pos + 1);
                    if (val_str.empty() && i + 1 < line.tokens.size()) {
                        val_str = line.tokens[++i];
                    }
                    if (!key.empty() && !val_str.empty()) {
                        try {
                            params[key] = eval_expression(val_str, params);
                        } catch (...) {
                            try { params[key] = parse_spice_number(val_str); } catch (...) {}
                        }
                    }
                } else if (line.tokens[i] != "=" &&
                           i + 1 < line.tokens.size() && line.tokens[i + 1] == "=" &&
                           i + 2 < line.tokens.size()) {
                    std::string key = to_lower(line.tokens[i]);
                    std::string val_str = line.tokens[i + 2];
                    if (!key.empty() && !val_str.empty()) {
                        try {
                            params[key] = eval_expression(val_str, params);
                        } catch (...) {
                            try { params[key] = parse_spice_number(val_str); } catch (...) {}
                        }
                    }
                    i += 2;
                }
            }
            continue;
        }

        // .model lines: prefix name with instance hierarchy to avoid
        // collisions between different instances, and evaluate {param}
        // expressions in the model parameters.
        if (first == ".model") {
            TokenizedLine new_line;
            new_line.line_number = line.line_number;
            new_line.tokens.push_back(line.tokens[0]);  // ".model"
            if (line.tokens.size() >= 2) {
                new_line.tokens.push_back(instance_prefix + "." + to_lower(line.tokens[1]));
            }
            // Remaining tokens: substitute {param} references in-place,
            // preserving surrounding syntax like parens: D(IS={KAIS} ...)
            for (size_t i = 2; i < line.tokens.size(); ++i) {
                new_line.tokens.push_back(subst_brace_params(line.tokens[i], params));
            }
            result.push_back(new_line);
            continue;
        }

        // Skip other dot commands that aren't element lines
        if (first[0] == '.') continue;

        char elem_type = std::tolower(static_cast<unsigned char>(first[0]));

        if (elem_type == 'x') {
            // Recursive X expansion
            // Parse: Xname n1 n2 ... subckt_name [key=val ...]
            std::string x_name = to_lower(line.tokens[0]);

            // Find subcircuit name: scan from end, first token without '='
            // that matches a known subcircuit
            std::string subckt_name;
            size_t subckt_pos = 0;
            for (size_t i = line.tokens.size(); i > 1; --i) {
                const std::string& tok = line.tokens[i - 1];
                if (tok.find('=') != std::string::npos) continue;
                std::string lower_tok = to_lower(tok);
                if (merged_defs.find(lower_tok) != merged_defs.end()) {
                    subckt_name = lower_tok;
                    subckt_pos = i - 1;
                    break;
                }
            }

            if (subckt_name.empty()) {
                fprintf(stderr, "Warning: X element '%s' references unknown subcircuit — skipping\n", x_name.c_str());
                continue;
            }

            const SubcircuitDef& sub_def = merged_defs.at(subckt_name);

            // Connections: tokens[1..subckt_pos-1]
            std::vector<std::string> sub_connections;
            for (size_t i = 1; i < subckt_pos; ++i) {
                sub_connections.push_back(subst_node(line.tokens[i]));
            }

            // Param overrides from this X line
            std::unordered_map<std::string, double> sub_params;
            for (size_t i = subckt_pos + 1; i < line.tokens.size(); ++i) {
                // Skip PSpice PARAMS: keyword separator
                std::string lower_ti = to_lower(line.tokens[i]);
                if (lower_ti == "params:" || lower_ti == "optional:") continue;
                auto eq_pos = line.tokens[i].find('=');
                if (eq_pos != std::string::npos) {
                    std::string key = to_lower(line.tokens[i].substr(0, eq_pos));
                    std::string val_str = line.tokens[i].substr(eq_pos + 1);
                    if (val_str.empty() && i + 1 < line.tokens.size()) {
                        val_str = line.tokens[++i];
                    }
                    if (!key.empty() && !val_str.empty()) {
                        try {
                            sub_params[key] = eval_expression(val_str, params);
                        } catch (...) {
                            try { sub_params[key] = parse_spice_number(val_str); } catch (...) {}
                        }
                    }
                } else if (line.tokens[i] != "=" &&
                           i + 1 < line.tokens.size() && line.tokens[i + 1] == "=" &&
                           i + 2 < line.tokens.size()) {
                    std::string key = to_lower(line.tokens[i]);
                    std::string val_str = line.tokens[i + 2];
                    if (!key.empty() && !val_str.empty()) {
                        try {
                            sub_params[key] = eval_expression(val_str, params);
                        } catch (...) {
                            try { sub_params[key] = parse_spice_number(val_str); } catch (...) {}
                        }
                    }
                    i += 2;
                }
            }

            // Recursive expansion
            std::string sub_prefix = instance_prefix + "." + x_name;
            auto expanded = expand_instance(sub_prefix, sub_def, sub_connections,
                                            sub_params, merged_defs, depth + 1,
                                            line.line_number, global_params,
                                            global_nodes);
            result.insert(result.end(), expanded.begin(), expanded.end());

        } else {
            // Regular element line — substitute nodes and evaluate param expressions
            int ncount = node_count_for_element(elem_type);

            // Q cards can have 3 or 4 nodes: Q name NC NB NE [NS] model [area]
            // Use the local model names to disambiguate token[4].
            if (elem_type == 'q' && line.tokens.size() > 4) {
                std::string tok4_lower = to_lower(line.tokens[4]);
                if (local_model_names.count(tok4_lower)) {
                    ncount = 3;  // token[4] is model, not substrate
                }
            }

            // E/G POLY form: E name np nn POLY(N) vc1+ vc1- ... coeffs
            // In POLY form, only tokens 1-2 are output nodes; token[3] is
            // POLY(N) and the control node pairs follow it.
            bool eg_poly = false;
            int eg_poly_dim = 0;
            if ((elem_type == 'e' || elem_type == 'g') &&
                line.tokens.size() > 3 &&
                to_lower(line.tokens[3]).substr(0, 4) == "poly") {
                eg_poly = true;
                ncount = 2;  // only np, nn are output nodes
                std::string pt = to_lower(line.tokens[3]);
                size_t paren = pt.find('(');
                if (paren != std::string::npos) {
                    size_t close = pt.find(')');
                    if (close != std::string::npos && close > paren)
                        eg_poly_dim = std::stoi(pt.substr(paren + 1, close - paren - 1));
                }
            }

            // E/G VALUE= form: E name np nn VALUE={expr} or VALUE = {expr}
            // E/G TABLE form: E name np nn TABLE {V(in)} = (x,y)...
            // These forms have only 2 output nodes (np, nn), not 4.
            bool eg_abm = false;
            if ((elem_type == 'e' || elem_type == 'g') && !eg_poly &&
                line.tokens.size() > 3) {
                std::string t3 = to_lower(line.tokens[3]);
                if (t3 == "value" || t3.substr(0, 6) == "value=" ||
                    t3 == "table" || t3.substr(0, 6) == "table{") {
                    eg_abm = true;
                    ncount = 2;
                }
            }

            TokenizedLine new_line;
            new_line.line_number = line.line_number;

            // Token 0: device name — prefix with instance hierarchy.
            // Convention: R1 in instance x1 becomes "x1.r1".
            // The parser's Pass 2 extracts the element type from the leaf
            // component (last dot-separated segment) of the device name.
            std::string orig_name = to_lower(line.tokens[0]);
            new_line.tokens.push_back(instance_prefix + "." + orig_name);

            // Tokens 1..ncount: node names — substitute
            for (int i = 1; i <= ncount && static_cast<size_t>(i) < line.tokens.size(); ++i) {
                new_line.tokens.push_back(subst_node(line.tokens[i]));
            }

            // E/G POLY form: pass POLY(N) token through, substitute
            // 2*N control node pairs, then let remaining coefficients
            // fall through to the normal value-evaluation path below.
            size_t value_start = static_cast<size_t>(1 + ncount);
            if (eg_poly && value_start < line.tokens.size()) {
                // Pass POLY(N) token through as-is
                new_line.tokens.push_back(line.tokens[value_start]);
                value_start++;
                // Handle split "(N)" token
                if (value_start < line.tokens.size() &&
                    line.tokens[value_start].front() == '(') {
                    new_line.tokens.push_back(line.tokens[value_start]);
                    value_start++;
                }
                // Substitute 2*N control node names
                int ctrl_nodes = 2 * eg_poly_dim;
                for (int k = 0; k < ctrl_nodes && value_start < line.tokens.size(); ++k) {
                    new_line.tokens.push_back(subst_node(line.tokens[value_start]));
                    value_start++;
                }
            }

            // E/G ABM (VALUE=/TABLE) form: substitute node references
            // inside the expression, then pass through.
            if (eg_abm) {
                for (size_t i = value_start; i < line.tokens.size(); ++i) {
                    const std::string& tok = line.tokens[i];
                    if (tok.find('(') != std::string::npos) {
                        std::string subst = subst_expr_nodes(tok, subst_node, instance_prefix);
                        new_line.tokens.push_back(subst);
                    } else {
                        new_line.tokens.push_back(tok);
                    }
                }
                result.push_back(new_line);
                continue;  // skip the normal value-evaluation loop below
            }

            // Special handling for H (CCVS) and F (CCCS) elements:
            // In simple form, token[3] is a Vsense device name that must be
            // prefixed with the instance hierarchy.
            // In POLY(N) form, token[3] is "POLY(N)" followed by N Vsense
            // names that each need prefixing, then polynomial coefficients.
            if ((elem_type == 'h' || elem_type == 'f') &&
                value_start < line.tokens.size()) {
                std::string tok3 = to_lower(line.tokens[value_start]);
                if (tok3.substr(0, 4) == "poly") {
                    // POLY(N) form: pass POLY token through, then prefix N
                    // Vsense names
                    new_line.tokens.push_back(line.tokens[value_start]);
                    value_start++;
                    // Handle split "(N)" token
                    if (value_start < line.tokens.size() &&
                        line.tokens[value_start].front() == '(') {
                        new_line.tokens.push_back(line.tokens[value_start]);
                        value_start++;
                    }
                    int poly_dim = 1;
                    size_t paren = tok3.find('(');
                    if (paren != std::string::npos) {
                        size_t close = tok3.find(')');
                        if (close != std::string::npos && close > paren)
                            poly_dim = std::stoi(tok3.substr(paren + 1,
                                                             close - paren - 1));
                    }
                    for (int k = 0; k < poly_dim && value_start < line.tokens.size(); ++k) {
                        std::string vs = to_lower(line.tokens[value_start]);
                        new_line.tokens.push_back(instance_prefix + "." + vs);
                        value_start++;
                    }
                } else {
                    // Simple form: single Vsense name
                    new_line.tokens.push_back(instance_prefix + "." + tok3);
                    value_start++;
                }
            }

            // Special handling for K (coupled inductor) elements:
            // tokens 1 and 2 (the two inductor device names) must be prefixed
            // with the instance hierarchy.  Token 3+ (coupling coefficient) are
            // ordinary values and pass through the normal evaluation path below.
            if (elem_type == 'k' && value_start + 1 < line.tokens.size()) {
                std::string l1_name = to_lower(line.tokens[value_start]);
                new_line.tokens.push_back(instance_prefix + "." + l1_name);
                value_start++;
                std::string l2_name = to_lower(line.tokens[value_start]);
                new_line.tokens.push_back(instance_prefix + "." + l2_name);
                value_start++;
            }

            // Remaining tokens: values, model names, key=val pairs — evaluate
            // param expressions where applicable
            for (size_t i = value_start; i < line.tokens.size(); ++i) {
                const std::string& tok = line.tokens[i];
                auto eq_pos = tok.find('=');
                if (eq_pos != std::string::npos) {
                    // key=value pair — evaluate the value part
                    std::string key = tok.substr(0, eq_pos);
                    std::string val_str = tok.substr(eq_pos + 1);
                    std::string eval_val = eval_value_token(val_str, params);
                    new_line.tokens.push_back(key + "=" + eval_val);
                } else {
                    // Check if this token is a local model name — prefix it
                    std::string tok_lower = to_lower(tok);
                    if (local_model_names.count(tok_lower)) {
                        new_line.tokens.push_back(instance_prefix + "." + tok_lower);
                    } else {
                        // Could be a value or a model name — try param evaluation
                        new_line.tokens.push_back(eval_value_token(tok, params));
                    }
                }
            }

            result.push_back(new_line);
        }
    }

    return result;
}

} // anonymous namespace

std::vector<TokenizedLine> expand_all_instances(
    const std::vector<TokenizedLine>& lines,
    const std::unordered_map<std::string, SubcircuitDef>& all_defs,
    const std::unordered_map<std::string, double>& global_params,
    const std::unordered_set<std::string>& global_nodes) {

    std::vector<TokenizedLine> result;
    result.reserve(lines.size());

    for (const auto& line : lines) {
        if (line.tokens.empty()) {
            result.push_back(line);
            continue;
        }

        std::string first = to_lower(line.tokens[0]);
        char elem_type = std::tolower(static_cast<unsigned char>(first[0]));

        if (first[0] != '.' && elem_type == 'x') {
            // Parse X element: Xname n1 n2 ... subckt_name [key=val ...]
            std::string x_name = to_lower(line.tokens[0]);

            // Find subcircuit name: scan from end, first token without '='
            // that's a known subcircuit
            std::string subckt_name;
            size_t subckt_pos = 0;
            for (size_t i = line.tokens.size(); i > 1; --i) {
                const std::string& tok = line.tokens[i - 1];
                if (tok.find('=') != std::string::npos) continue;
                std::string lower_tok = to_lower(tok);
                if (all_defs.find(lower_tok) != all_defs.end()) {
                    subckt_name = lower_tok;
                    subckt_pos = i - 1;
                    break;
                }
            }

            if (subckt_name.empty()) {
                fprintf(stderr, "Warning: Line %d: X element '%s' references unknown subcircuit — skipping\n",
                        line.line_number, x_name.c_str());
                continue;
            }

            const SubcircuitDef& def = all_defs.at(subckt_name);

            // Connections: tokens[1..subckt_pos-1]
            std::vector<std::string> connections;
            for (size_t i = 1; i < subckt_pos; ++i) {
                connections.push_back(to_lower(line.tokens[i]));
            }

            // Verify port count
            if (connections.size() != def.ports.size()) {
                if (connections.size() > def.ports.size()) {
                    fprintf(stderr, "Warning: Line %d: Subcircuit '%s' expects %zu port(s) but got %zu connection(s) — truncating\n",
                            line.line_number, subckt_name.c_str(), def.ports.size(), connections.size());
                    connections.resize(def.ports.size());
                } else {
                    fprintf(stderr, "Warning: Line %d: Subcircuit '%s' expects %zu port(s) but got %zu connection(s) — skipping\n",
                            line.line_number, subckt_name.c_str(), def.ports.size(), connections.size());
                    continue;
                }
            }

            // Parse parameter overrides — use global_params so that
            // expressions like R={myR} can reference top-level .param values.
            std::unordered_map<std::string, double> instance_params;
            for (size_t i = subckt_pos + 1; i < line.tokens.size(); ++i) {
                auto eq_pos = line.tokens[i].find('=');
                if (eq_pos != std::string::npos) {
                    std::string key = to_lower(line.tokens[i].substr(0, eq_pos));
                    std::string val_str = line.tokens[i].substr(eq_pos + 1);
                    try {
                        instance_params[key] = parse_spice_number(val_str);
                    } catch (...) {
                        // Try eval_expression with global_params context
                        try {
                            instance_params[key] = eval_expression(val_str, global_params);
                        } catch (...) {
                            fprintf(stderr, "Warning: Line %d: Cannot parse parameter value '%s' — defaulting to 0\n",
                                    line.line_number, val_str.c_str());
                            instance_params[key] = 0.0;
                        }
                    }
                }
            }

            // Expand
            auto expanded = expand_instance(x_name, def, connections,
                                            instance_params, all_defs, 1,
                                            line.line_number, global_params,
                                            global_nodes);
            result.insert(result.end(), expanded.begin(), expanded.end());
        } else {
            result.push_back(line);
        }
    }

    return result;
}

} // namespace neospice
