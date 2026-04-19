#include "parser/subcircuit_expand.hpp"
#include "parser/expression.hpp"
#include "core/types.hpp"
#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace neospice {

namespace {

std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

/// Ground node names that are never substituted (global nodes).
bool is_ground(const std::string& name) {
    std::string lower = to_lower(name);
    return lower == "0" || lower == "gnd";
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
        case 'h': case 'f':
            return 2;
        case 'q':
            return 3;  // Q has 3 node tokens (C B E) before model; substrate is optional
        default:
            return 0;
    }
}

/// Check if a token looks like a parameter expression (contains braces or
/// references a known parameter name).
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
                auto eq_pos = tok.find('=');
                if (eq_pos != std::string::npos) {
                    std::string key = to_lower(tok.substr(0, eq_pos));
                    std::string val = tok.substr(eq_pos + 1);
                    current_def.default_params.emplace_back(key, val);
                    seen_param = true;
                } else {
                    if (seen_param) {
                        throw ParseError("Port '" + tok +
                                         "' after parameter defaults in .subckt header");
                    }
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
/// Returns the set of internal node names (not ports, not ground).
std::unordered_set<std::string> collect_internal_nodes(
    const std::vector<TokenizedLine>& body,
    const std::unordered_set<std::string>& port_set,
    const std::unordered_map<std::string, SubcircuitDef>& all_defs) {

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
            for (int i = 0; i < ncount; ++i) {
                node_positions.push_back(static_cast<size_t>(1 + i));
            }
        }

        // Check each node position
        for (size_t pos : node_positions) {
            if (pos >= line.tokens.size()) continue;
            std::string node = to_lower(line.tokens[pos]);
            if (!is_ground(node) && port_set.find(node) == port_set.end()) {
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
std::vector<TokenizedLine> expand_instance(
    const std::string& instance_prefix,
    const SubcircuitDef& def,
    const std::vector<std::string>& connections,
    const std::unordered_map<std::string, double>& instance_params,
    const std::unordered_map<std::string, SubcircuitDef>& all_defs,
    int depth,
    int line_number,
    const std::unordered_map<std::string, double>& global_params) {

    if (depth > MAX_SUBCIRCUIT_DEPTH) {
        throw ParseError("Line " + std::to_string(line_number) +
                         ": Maximum subcircuit nesting depth (" +
                         std::to_string(MAX_SUBCIRCUIT_DEPTH) +
                         ") exceeded — possible infinite recursion in subcircuit '" +
                         def.name + "'");
    }

    // Verify port count matches
    if (connections.size() != def.ports.size()) {
        throw ParseError("Line " + std::to_string(line_number) +
                         ": Subcircuit '" + def.name + "' expects " +
                         std::to_string(def.ports.size()) + " port(s) but got " +
                         std::to_string(connections.size()) + " connection(s)");
    }

    // Build port-to-connection map
    std::unordered_map<std::string, std::string> node_map;
    for (size_t i = 0; i < def.ports.size(); ++i) {
        node_map[def.ports[i]] = connections[i];
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
    auto internal_nodes = collect_internal_nodes(body_lines, port_set, merged_defs);

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
        if (is_ground(lower)) return name;  // ground is global
        auto it = node_map.find(lower);
        if (it != node_map.end()) return it->second;
        // Not a known port or internal node — treat as global (shouldn't happen
        // in well-formed subcircuits, but be safe)
        return instance_prefix + "." + lower;
    };

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
                    try {
                        params[key] = eval_expression(val_str, params);
                    } catch (...) {
                        // Ignore param evaluation failures during body processing
                    }
                }
            }
            continue;
        }

        // Pass through .model lines as-is (model names are global)
        if (first == ".model") {
            result.push_back(line);
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
                throw ParseError("X element '" + x_name +
                                 "' references unknown subcircuit");
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
                auto eq_pos = line.tokens[i].find('=');
                if (eq_pos != std::string::npos) {
                    std::string key = to_lower(line.tokens[i].substr(0, eq_pos));
                    std::string val_str = line.tokens[i].substr(eq_pos + 1);
                    try {
                        sub_params[key] = eval_expression(val_str, params);
                    } catch (...) {
                        sub_params[key] = parse_spice_number(val_str);
                    }
                }
            }

            // Recursive expansion
            std::string sub_prefix = instance_prefix + "." + x_name;
            auto expanded = expand_instance(sub_prefix, sub_def, sub_connections,
                                            sub_params, merged_defs, depth + 1,
                                            line.line_number, global_params);
            result.insert(result.end(), expanded.begin(), expanded.end());

        } else {
            // Regular element line — substitute nodes and evaluate param expressions
            int ncount = node_count_for_element(elem_type);

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

            // Special handling for H (CCVS) and F (CCCS) elements:
            // token[3] (first token after the 2 nodes) is a Vsense device name
            // that must be prefixed with the instance hierarchy, not evaluated
            // as a parameter expression.
            size_t value_start = static_cast<size_t>(1 + ncount);
            if ((elem_type == 'h' || elem_type == 'f') &&
                value_start < line.tokens.size()) {
                std::string vsense = to_lower(line.tokens[value_start]);
                new_line.tokens.push_back(instance_prefix + "." + vsense);
                value_start++;
            }

            // Special handling for Q (BJT) elements: ncount=3 covers C,B,E but
            // Q can have an optional 4th substrate node before the model name.
            // If the token right after E (token[4]) is NOT a model name and NOT
            // a key=value pair, treat it as a substrate node and substitute it.
            if (elem_type == 'q' && value_start < line.tokens.size()) {
                const std::string& tok4 = line.tokens[value_start];
                // If it contains '=' it's a param; otherwise check if it could
                // be a node (not just a bare number or model name).
                // We substitute it as a node to be safe — the parser will
                // later determine whether it's a model name or substrate node.
                if (tok4.find('=') == std::string::npos) {
                    new_line.tokens.push_back(subst_node(tok4));
                    value_start++;
                }
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
                    // Could be a value or a model name — try param evaluation
                    new_line.tokens.push_back(eval_value_token(tok, params));
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
    const std::unordered_map<std::string, double>& global_params) {

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
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": X element '" + x_name +
                                 "' references unknown subcircuit");
            }

            const SubcircuitDef& def = all_defs.at(subckt_name);

            // Connections: tokens[1..subckt_pos-1]
            std::vector<std::string> connections;
            for (size_t i = 1; i < subckt_pos; ++i) {
                connections.push_back(to_lower(line.tokens[i]));
            }

            // Verify port count
            if (connections.size() != def.ports.size()) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": Subcircuit '" + subckt_name + "' expects " +
                                 std::to_string(def.ports.size()) +
                                 " port(s) but got " +
                                 std::to_string(connections.size()) +
                                 " connection(s)");
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
                            throw ParseError("Line " + std::to_string(line.line_number) +
                                             ": Cannot parse parameter value '" +
                                             val_str + "'");
                        }
                    }
                }
            }

            // Expand
            auto expanded = expand_instance(x_name, def, connections,
                                            instance_params, all_defs, 1,
                                            line.line_number, global_params);
            result.insert(result.end(), expanded.begin(), expanded.end());
        } else {
            result.push_back(line);
        }
    }

    return result;
}

} // namespace neospice
