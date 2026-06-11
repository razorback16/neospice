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

/// Return the index of the first PSpice parameter-section keyword
/// (PARAMS:/OPTIONAL:/TEXT:) in an X-element token list, or tokens.size()
/// if none.  The subcircuit name and connection nodes precede this boundary;
/// everything at or after it is a parameter assignment.  Without this boundary
/// the "scan from end for a known subckt name" loop can latch onto a parameter
/// key that happens to share a name with a defined subcircuit (e.g. a CMRR
/// subckt invoked as "X a b c CMRR PARAMS: CMRR=150 ...").
size_t x_param_section_start(const std::vector<std::string>& tokens) {
    for (size_t i = 1; i < tokens.size(); ++i) {
        std::string lt = to_lower(tokens[i]);
        if (lt == "params:" || lt == "optional:" || lt == "text:")
            return i;
    }
    return tokens.size();
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

/// Extract up to `want` node atoms from `tokens` starting at token index
/// `start`, following ngspice gettok_node semantics: '(', ')', ',' and
/// whitespace ALL delimit. This makes "(n+,n-) (nc+,nc-)", "(n+ n-)",
/// "n+ n-" and bare "n+,n-" all yield the same atom list. On return,
/// `tokens_consumed` is the number of whole tokens fully consumed (so the
/// caller can resume reading non-node tokens, e.g. an S model name) — a
/// partially-consumed token is NOT counted as consumed.
std::vector<std::string> extract_node_atoms(
    const std::vector<std::string>& tokens, size_t start, int want,
    size_t& tokens_consumed, size_t start_off = 0) {
    std::vector<std::string> atoms;
    size_t idx = start;
    size_t off = start_off;
    std::string cur;
    auto flush = [&]() {
        if (!cur.empty()) { atoms.push_back(cur); cur.clear(); }
    };
    while (idx < tokens.size() && static_cast<int>(atoms.size()) < want) {
        const std::string& tok = tokens[idx];
        bool stop = false;
        for (; off < tok.size(); ++off) {
            char c = tok[off];
            if (c == '(' || c == ')' || c == ',') {
                flush();
                if (static_cast<int>(atoms.size()) >= want) { ++off; stop = true; break; }
            } else {
                cur += c;
            }
        }
        if (stop) {
            // If the delimiter that satisfied `want` was the last char of this
            // token, the whole token is consumed; advance so the caller resumes
            // at the next token.
            if (off >= tok.size()) { ++idx; off = 0; }
            break;
        }
        // Reached end of token → whitespace delimiter.
        flush();
        ++idx;
        off = 0;
    }
    flush();
    // Determine the next whole token the caller should resume from. If we are
    // mid-token (off in the interior), the remaining chars are part of the same
    // physical token and the caller (which works token-by-token) must skip it.
    if (idx < tokens.size() && off > 0 && off < tokens[idx].size())
        tokens_consumed = (idx - start) + 1;
    else
        tokens_consumed = idx - start;
    return atoms;
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

/// Substitute bare parameter identifiers in an expression string with their
/// numeric values.  Identifiers followed by '(' are treated as function calls
/// (V, I, IF, MIN, MAX, etc.) and are left alone.
std::string subst_param_names(
    const std::string& expr,
    const std::unordered_map<std::string, double>& params) {
    std::string result;
    result.reserve(expr.size());
    size_t i = 0;
    while (i < expr.size()) {
        if (std::isalpha(static_cast<unsigned char>(expr[i])) || expr[i] == '_') {
            size_t start = i;
            while (i < expr.size() &&
                   (std::isalnum(static_cast<unsigned char>(expr[i])) || expr[i] == '_'))
                ++i;
            std::string name = expr.substr(start, i - start);
            // Check if followed by '(' — if so, it's a function call, not a param
            size_t tmp = i;
            while (tmp < expr.size() && std::isspace(static_cast<unsigned char>(expr[tmp]))) ++tmp;
            if (tmp < expr.size() && expr[tmp] == '(') {
                result += name;
                continue;
            }
            // Check if it's a known parameter
            std::string lname = to_lower(name);
            auto it = params.find(lname);
            if (it != params.end()) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.15g", it->second);
                result += buf;
            } else {
                result += name;
            }
        } else {
            result += expr[i];
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

/// Emit the value/expression tokens of an E/G-ABM (VALUE=/TABLE) or B-source
/// line, inlining subcircuit-local .func calls and substituting node and
/// parameter references.
///
/// The value tokens (line.tokens[value_start..]) are joined into a single
/// expression string first, because a user-function call (and the braced
/// VALUE={...} body) can be split across several whitespace-separated tokens.
/// Per-token func expansion would miss those split calls and leave dangling
/// V(.,.) fragments. Operating on the joined string lets expand_funcs match
/// whole `fname(...)` calls, after which node refs that came from the inlined
/// func arguments still flow through subst_expr_nodes (so they get the
/// instance prefix) and subckt-local params are replaced numerically.
void emit_abm_expr_tokens(
    TokenizedLine& new_line,
    const TokenizedLine& line,
    size_t value_start,
    const std::unordered_map<std::string, FuncDef>& local_funcs,
    const std::function<std::string(const std::string&)>& subst_node,
    const std::string& instance_prefix,
    const std::unordered_map<std::string, double>& params) {
    if (value_start >= line.tokens.size()) return;

    std::string joined;
    for (size_t i = value_start; i < line.tokens.size(); ++i) {
        if (!joined.empty()) joined += ' ';
        joined += line.tokens[i];
    }

    if (!local_funcs.empty()) {
        joined = expand_funcs(joined, local_funcs);
    }
    if (joined.find('(') != std::string::npos) {
        joined = subst_expr_nodes(joined, subst_node, instance_prefix);
    }
    joined = subst_param_names(joined, params);

    // Re-split into the same token structure the original tokenizer produced,
    // so the downstream E/G/B parser sees the keyword, the braced expression,
    // and any following table-point tokens as distinct tokens.
    //
    // The downstream parser identifies the source form by the FIRST value token
    // ("VALUE", "VALUE=", "TABLE", "I=", ...). Crucially, the E/G TABLE form
    //   E name np nn TABLE {expr} (x1,y1) (x2,y2) ...
    // requires the keyword, the {expr} body, and each (x,y) point to remain
    // separate whitespace-delimited tokens (it counts tokens and scans points
    // positionally). A whole-line join with only a leading-keyword split would
    // glue every table point into one token and the TABLE parse would fail.
    //
    // Re-split on whitespace but keep any {...} brace group as a single token
    // (the braced expression may contain spaces, e.g. "{ v(a) + v(b) }"),
    // matching split_tokens() in the tokenizer.
    size_t i = 0;
    const size_t n = joined.size();
    while (i < n) {
        while (i < n && (joined[i] == ' ' || joined[i] == '\t')) ++i;
        if (i >= n) break;
        size_t start = i;
        int brace_depth = 0;
        while (i < n) {
            char c = joined[i];
            if (c == '{') ++brace_depth;
            else if (c == '}') { if (brace_depth > 0) --brace_depth; }
            else if ((c == ' ' || c == '\t') && brace_depth == 0) break;
            ++i;
        }
        new_line.tokens.push_back(joined.substr(start, i - start));
    }
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
                std::string tok = line.tokens[i];
                std::string tok_lower = to_lower(tok);
                // A PSpice section keyword may be glued to the first parameter,
                // e.g. "Params:B0=1" — strip the prefix so the remainder ("B0=1")
                // parses as a normal key=value.
                for (const char* kw : {"params:", "optional:", "text:"}) {
                    size_t kl = std::char_traits<char>::length(kw);
                    if (tok_lower.size() > kl && tok_lower.compare(0, kl, kw) == 0) {
                        if (kw[0] == 'p') seen_param = true;
                        tok = tok.substr(kl);
                        tok_lower = to_lower(tok);
                        break;
                    }
                }
                // PSpice section keywords — skip the keyword itself
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
            // Find subcircuit name: last token without '=' that's a known
            // subcircuit, searching only before any PSpice parameter section.
            std::string subckt_name;
            size_t subckt_pos = 0;
            for (size_t i = x_param_section_start(line.tokens); i > 1; --i) {
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

            // E/G POLY: only 2 output nodes, then POLY(N) + 2*N control node
            // atoms. Handles control pairs as "(cp,cn)", "(cp cn)", bare
            // "cp cn", and the comma-glued "POLY(N),(cp,cn),..." form via the
            // gettok_node atom scanner.
            if ((elem_type == 'e' || elem_type == 'g') &&
                line.tokens.size() > 3 &&
                to_lower(line.tokens[3]).substr(0, 4) == "poly") {
                ncount = 2;  // output nodes only
                std::string pt = to_lower(line.tokens[3]);
                int pdim = 1;
                bool dim_in_token = false;
                size_t paren = pt.find('(');
                if (paren != std::string::npos) {
                    size_t close = pt.find(')');
                    if (close != std::string::npos && close > paren) {
                        pdim = std::stoi(pt.substr(paren + 1, close - paren - 1));
                        dim_in_token = true;
                    }
                }
                size_t scan_tok = 3, scan_off = 0;
                if (dim_in_token) {
                    scan_off = pt.find(')') + 1;  // resume after the glued count
                } else {
                    scan_tok = 4;
                    if (scan_tok < line.tokens.size() && line.tokens[scan_tok].front() == '(')
                        ++scan_tok;  // skip split "(N)" token
                }
                size_t consumed = 0;
                auto atoms = extract_node_atoms(line.tokens, scan_tok, 2 * pdim,
                                                consumed, scan_off);
                for (const auto& a : atoms) {
                    std::string an = to_lower(a);
                    if (!is_global_node(an, global_nodes) &&
                        port_set.find(an) == port_set.end())
                        internal_nodes.insert(an);
                }
            }

            // S (VSWITCH): 4 node atoms (n+ n- nc+ nc-) which may use any
            // gettok_node form, including two parenthesized groups
            // "(n+,n-) (nc+,nc-)" (cadlab regulators). Collect via atom scan.
            if (elem_type == 's') {
                size_t consumed = 0;
                auto atoms = extract_node_atoms(line.tokens, 1, 4, consumed);
                for (const auto& a : atoms) {
                    std::string an = to_lower(a);
                    if (!is_global_node(an, global_nodes) &&
                        port_set.find(an) == port_set.end())
                        internal_nodes.insert(an);
                }
                continue;  // node_positions path below not used for S
            }

            // F/H with a parenthesized OUTPUT group, e.g. "Fl (Ground,0) Vmon 3e-4":
            // collect the 2 output node atoms (control is a Vsense name, not a node).
            if ((elem_type == 'f' || elem_type == 'h') &&
                line.tokens.size() > 1 &&
                line.tokens[1].size() > 1 && line.tokens[1][0] == '(') {
                size_t consumed = 0;
                auto atoms = extract_node_atoms(line.tokens, 1, 2, consumed);
                for (const auto& a : atoms) {
                    std::string an = to_lower(a);
                    if (!is_global_node(an, global_nodes) &&
                        port_set.find(an) == port_set.end())
                        internal_nodes.insert(an);
                }
                continue;
            }

            // E/G linear form with a parenthesized OUTPUT group, e.g.
            // "Ebg (102,0) (Input,Ground) 1" — read all 4 nodes via atom scan.
            if ((elem_type == 'e' || elem_type == 'g') &&
                line.tokens.size() > 1 &&
                to_lower(line.tokens.size() > 3 ? line.tokens[3] : "").substr(0, 4) != "poly" &&
                line.tokens[1].size() > 1 && line.tokens[1][0] == '(' &&
                to_lower(line.tokens[1]).substr(0, 4) != "poly") {
                size_t consumed = 0;
                auto atoms = extract_node_atoms(line.tokens, 1, 4, consumed);
                for (const auto& a : atoms) {
                    std::string an = to_lower(a);
                    if (!is_global_node(an, global_nodes) &&
                        port_set.find(an) == port_set.end())
                        internal_nodes.insert(an);
                }
                continue;
            }

            // E/G linear form with parenthesized control nodes — handles both
            // comma "(nc+,nc-)" and space "(nc+ nc-)" forms (the latter spans
            // two whitespace-split tokens). Output nodes np, nn stay at 1,2.
            if ((elem_type == 'e' || elem_type == 'g') &&
                line.tokens.size() > 3 &&
                to_lower(line.tokens[3]).substr(0, 4) != "poly") {
                const std::string& t3 = line.tokens[3];
                if (t3.size() > 1 && t3[0] == '(') {
                    ncount = 2;  // only np, nn are output nodes
                    size_t consumed = 0;
                    auto atoms = extract_node_atoms(line.tokens, 3, 2, consumed);
                    for (const auto& a : atoms) {
                        std::string an = to_lower(a);
                        if (!is_global_node(an, global_nodes) &&
                            port_set.find(an) == port_set.end())
                            internal_nodes.insert(an);
                    }
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
    const std::unordered_set<std::string>& global_nodes,
    const std::unordered_set<std::string>& global_model_names) {

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

    // Normalize a stray leading '(' glued onto the POLY keyword of an E/G
    // element (e.g. "E4 97 22 (POLY(1) (99,98) ..." in some ADI macromodels).
    // Strip it once here so both the internal-node collection pass and the
    // emit pass below see a clean "POLY(1)" token and take the POLY path.
    for (auto& bl : body_lines) {
        if (bl.tokens.size() <= 3 || bl.tokens[0].empty()) continue;
        char et = std::tolower(static_cast<unsigned char>(bl.tokens[0][0]));
        if (et != 'e' && et != 'g') continue;
        std::string& t3 = bl.tokens[3];
        if (t3.size() > 1 && t3[0] == '(' &&
            to_lower(t3).compare(1, 4, "poly") == 0)
            t3.erase(0, 1);
    }

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

    // PRE-SCAN: collect all body .param lines into the params map before
    // processing any element or .model lines.  SPICE defines .param as
    // order-independent within a subcircuit scope; ngspice pre-scans all
    // params before evaluating elements.  Without this, elements that appear
    // textually before the defining .param line (e.g. DB3, TSV731, TL598,
    // Integral) produce "Unknown parameter 'X' — defaulting to 0" warnings.
    {
        std::vector<std::pair<std::string, std::string>> body_raw;
        for (const auto& bline : body_lines) {
            if (bline.tokens.empty()) continue;
            if (to_lower(bline.tokens[0]) != ".param") continue;
            for (size_t i = 1; i < bline.tokens.size(); ++i) {
                auto eq = bline.tokens[i].find('=');
                if (eq != std::string::npos) {
                    std::string key = to_lower(bline.tokens[i].substr(0, eq));
                    std::string val = bline.tokens[i].substr(eq + 1);
                    if (val.empty() && i + 1 < bline.tokens.size())
                        val = bline.tokens[++i];
                    if (!key.empty() && !val.empty())
                        body_raw.emplace_back(key, val);
                } else if (bline.tokens[i] != "=" &&
                           i + 1 < bline.tokens.size() &&
                           bline.tokens[i + 1] == "=" &&
                           i + 2 < bline.tokens.size()) {
                    body_raw.emplace_back(to_lower(bline.tokens[i]), bline.tokens[i + 2]);
                    i += 2;
                }
            }
        }
        if (!body_raw.empty()) {
            // Build a combined raw list: current params (as numeric strings)
            // followed by body params.  resolve_params() topo-sorts them so
            // forward references among body params (e.g. .param af=bf/(bf+1)
            // before .param BF=5) are handled correctly.
            std::vector<std::pair<std::string, std::string>> combined_raw;
            for (const auto& [k, v] : params) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.15g", v);
                combined_raw.emplace_back(k, std::string(buf));
            }
            for (auto& pr : body_raw)
                combined_raw.push_back(pr);
            try {
                auto resolved = resolve_params(combined_raw);
                for (auto& [k, v] : resolved)
                    params[k] = v;
            } catch (...) {
                // resolve_params may throw on genuine circular dependencies;
                // fall back to sequential evaluation (existing behaviour in
                // the main loop below will handle them one-by-one).
            }
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

    // Pre-collect subcircuit-local .func definitions so that user-function
    // calls inside device VALUE/expression tokens (E/G ABM, B-sources) can be
    // textually inlined here — the downstream ASRC expression parser has no
    // user-func concept. Without this, subckt-local .func lines are dropped at
    // the "skip dot commands" continue below and the calls reach the runtime
    // unexpanded, killing the affected sources.
    std::unordered_map<std::string, FuncDef> local_funcs;
    for (const auto& line : body_lines) {
        if (!line.tokens.empty() && to_lower(line.tokens[0]) == ".func") {
            parse_func_def(line.tokens, local_funcs);
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

            // Find subcircuit name: scan from end (before any PSpice parameter
            // section), first token without '=' that matches a known subcircuit
            std::string subckt_name;
            size_t subckt_pos = 0;
            for (size_t i = x_param_section_start(line.tokens); i > 1; --i) {
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
                                            global_nodes, global_model_names);
            result.insert(result.end(), expanded.begin(), expanded.end());

        } else {
            // Regular element line — substitute nodes and evaluate param expressions
            int ncount = node_count_for_element(elem_type);

            // Q cards can have 3 or 4 nodes: Q name NC NB NE [NS] model [area]
            // Use the local model names to disambiguate token[4].
            if (elem_type == 'q' && line.tokens.size() > 4) {
                std::string tok4_lower = to_lower(line.tokens[4]);
                // token[4] is the model (3-node Q) if it names a model defined
                // either in the subckt body OR at top level (ngspice resolves
                // the model against the global table regardless of scope).
                if (local_model_names.count(tok4_lower) ||
                    global_model_names.count(tok4_lower)) {
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
                    t3.substr(0, 6) == "value{" ||
                    t3 == "table" || t3.substr(0, 6) == "table{") {
                    eg_abm = true;
                    ncount = 2;
                }
            }

            // E/G linear form with parenthesized control nodes:
            // E name np nn (nc+,nc-) gain  /  G name np nn (nc+,nc-) gm
            // token[3] is a single token containing (nc+,nc-) — only 2 output nodes.
            // Triggers for both "(nc+,nc-)" and space-in-parens "(nc+ nc-)".
            bool eg_paren_ctrl = false;
            if ((elem_type == 'e' || elem_type == 'g') && !eg_poly && !eg_abm &&
                line.tokens.size() > 3) {
                const std::string& t3 = line.tokens[3];
                if (t3.size() > 1 && t3[0] == '(') {
                    eg_paren_ctrl = true;
                    ncount = 2;  // only np, nn are output nodes
                }
            }

            // E/G linear form with a parenthesized OUTPUT group, e.g.
            // "Ebg (102,0) (Input,Ground) 1" or "Gq (Input,Ground) (Input,9) 2e-5"
            // (cadlab regulators). The output pair is its own token, so the
            // by-position logic above mis-detects the control pair. Read all 4
            // nodes with the gettok_node atom scanner; the gain/gm follows.
            bool eg_linear_paren_out = false;
            if ((elem_type == 'e' || elem_type == 'g') && !eg_poly && !eg_abm &&
                !eg_paren_ctrl && line.tokens.size() > 1) {
                const std::string& t1 = line.tokens[1];
                if (t1.size() > 1 && t1[0] == '(')
                    eg_linear_paren_out = true;
            }

            // F/H (CCCS/CCVS) with a parenthesized OUTPUT group, e.g.
            // "Fl (Ground,0) Vmon 3.0E-4" (cadlab). The output pair is its own
            // token; read the 2 output node atoms then resume with the Vsense
            // name(s) / coeffs via the existing F/H handling below.
            bool fh_paren_out = false;
            if ((elem_type == 'f' || elem_type == 'h') && line.tokens.size() > 1) {
                const std::string& t1 = line.tokens[1];
                if (t1.size() > 1 && t1[0] == '(')
                    fh_paren_out = true;
            }

            TokenizedLine new_line;
            new_line.line_number = line.line_number;

            // Token 0: device name — prefix with instance hierarchy.
            // Convention: R1 in instance x1 becomes "x1.r1".
            // The parser's Pass 2 extracts the element type from the leaf
            // component (last dot-separated segment) of the device name.
            std::string orig_name = to_lower(line.tokens[0]);
            new_line.tokens.push_back(instance_prefix + "." + orig_name);

            // S (VSWITCH): emit 4 substituted node atoms (handling any
            // gettok_node form incl. two parenthesized groups) followed by the
            // model name and any trailing tokens, then continue. Must stay in
            // sync with the node-collection pass above.
            if (elem_type == 's') {
                size_t consumed = 0;
                auto atoms = extract_node_atoms(line.tokens, 1, 4, consumed);
                for (const auto& a : atoms)
                    new_line.tokens.push_back(subst_node(a));
                for (size_t i = 1 + consumed; i < line.tokens.size(); ++i) {
                    std::string tok_lower = to_lower(line.tokens[i]);
                    if (local_model_names.count(tok_lower))
                        new_line.tokens.push_back(instance_prefix + "." + tok_lower);
                    else
                        new_line.tokens.push_back(line.tokens[i]);
                }
                result.push_back(new_line);
                continue;
            }

            // E/G linear form with a parenthesized output group: emit the 4
            // substituted nodes (out+ out- nc+ nc-) followed by the gain and
            // any trailing tokens. Emitted as bare nodes — the netlist parser's
            // atom scanner accepts this just like the original parenthesized
            // form.
            if (eg_linear_paren_out) {
                size_t consumed = 0;
                auto atoms = extract_node_atoms(line.tokens, 1, 4, consumed);
                for (const auto& a : atoms)
                    new_line.tokens.push_back(subst_node(a));
                for (size_t i = 1 + consumed; i < line.tokens.size(); ++i) {
                    std::string tok = line.tokens[i];
                    tok = subst_param_names(tok, params);
                    new_line.tokens.push_back(tok);
                }
                result.push_back(new_line);
                continue;
            }

            // F/H with parenthesized output group: emit the 2 substituted
            // output node atoms and advance value_start past them so the F/H
            // Vsense/POLY handling below resumes at the right token.
            size_t value_start = static_cast<size_t>(1 + ncount);
            if (fh_paren_out) {
                size_t consumed = 0;
                auto atoms = extract_node_atoms(line.tokens, 1, 2, consumed);
                for (const auto& a : atoms)
                    new_line.tokens.push_back(subst_node(a));
                value_start = 1 + consumed;
            } else {
                // Tokens 1..ncount: node names — substitute
                for (int i = 1; i <= ncount && static_cast<size_t>(i) < line.tokens.size(); ++i) {
                    new_line.tokens.push_back(subst_node(line.tokens[i]));
                }
            }

            // E/G POLY form: emit "POLY(N)", substitute the 2*N control node
            // atoms, then let remaining coefficients fall through to the normal
            // value-evaluation path below. Handles the comma-glued
            // "POLY(N),(cp,cn),..." form where control nodes are stuck onto the
            // POLY token.
            if (eg_poly && value_start < line.tokens.size()) {
                const std::string& poly_tok = line.tokens[value_start];
                std::string poly_lc = to_lower(poly_tok);
                size_t close = poly_lc.find(')');
                bool dim_in_token = (poly_lc.find('(') != std::string::npos &&
                                     close != std::string::npos);
                size_t scan_tok = value_start;
                size_t scan_off = 0;
                if (dim_in_token) {
                    if (eg_poly_dim == 0) {
                        size_t op = poly_lc.find('(');
                        eg_poly_dim = std::stoi(poly_lc.substr(op + 1, close - op - 1));
                    }
                    // Emit just "POLY(N)" — drop any glued control nodes.
                    new_line.tokens.push_back(poly_tok.substr(0, close + 1));
                    scan_off = close + 1;  // resume after the count in same token
                } else {
                    new_line.tokens.push_back(poly_tok);  // "POLY"
                    value_start++;
                    scan_tok = value_start;
                    // Separate "(N)" dimension token (number only, no comma).
                    if (value_start < line.tokens.size() &&
                        line.tokens[value_start].front() == '(' &&
                        line.tokens[value_start].find(',') == std::string::npos) {
                        if (eg_poly_dim == 0) {
                            const std::string& dt = line.tokens[value_start];
                            size_t c2 = dt.find(')');
                            if (c2 != std::string::npos && c2 > 1)
                                eg_poly_dim = std::stoi(dt.substr(1, c2 - 1));
                        }
                        new_line.tokens.push_back(line.tokens[value_start]);
                        value_start++;
                        scan_tok = value_start;
                    }
                }
                // Substitute the 2*N control node atoms.
                size_t consumed = 0;
                auto atoms = extract_node_atoms(line.tokens, scan_tok,
                                                2 * eg_poly_dim, consumed, scan_off);
                for (const auto& a : atoms)
                    new_line.tokens.push_back(subst_node(a));
                value_start = scan_tok + consumed;
            }

            // E/G linear form with parenthesized control nodes: read the 2
            // control node atoms (handling comma "(nc+,nc-)" and space-in-parens
            // "(nc+ nc-)" forms) and emit them as bare substituted nodes. The
            // gain/gm token(s) follow at the resume index.
            if (eg_paren_ctrl && value_start < line.tokens.size()) {
                size_t consumed = 0;
                auto atoms = extract_node_atoms(line.tokens, value_start, 2, consumed);
                for (const auto& a : atoms)
                    new_line.tokens.push_back(subst_node(a));
                value_start += consumed;
            }

            // E/G ABM (VALUE=/TABLE) form: substitute node references
            // inside the expression, then pass through.
            if (eg_abm) {
                emit_abm_expr_tokens(new_line, line, value_start, local_funcs,
                                     subst_node, instance_prefix, params);
                result.push_back(new_line);
                continue;  // skip the normal value-evaluation loop below
            }

            // B-source (ASRC) expressions: like E/G ABM, the I=/V= expression
            // references node voltages and subcircuit-local .param values.
            // Substitute node names (so v(local) -> v(x1.local)) and inline
            // local params so the runtime expression evaluator resolves them.
            if (elem_type == 'b') {
                emit_abm_expr_tokens(new_line, line, value_start, local_funcs,
                                     subst_node, instance_prefix, params);
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
                    // POLY(N) form. The N Vsense names may be plain space-
                    // separated tokens, or glued onto / parenthesised around the
                    // POLY keyword via commas and parens (e.g. "POLY(1),(V1)" or
                    // "POLY(1) (V1)"). ngspice's MIFgettok treats ( ) , as
                    // whitespace, so collect the VS names with extract_node_atoms
                    // (which mirrors that) and emit each one prefixed as a clean
                    // separate token, preceded by a bare "POLY(N)" keyword.
                    int poly_dim = 1;
                    bool dim_in_token = false;
                    size_t paren = tok3.find('(');
                    size_t close = tok3.find(')');
                    if (paren != std::string::npos && close != std::string::npos &&
                        close > paren) {
                        poly_dim = std::stoi(tok3.substr(paren + 1,
                                                         close - paren - 1));
                        dim_in_token = true;
                    }
                    size_t scan_tok = value_start;
                    size_t scan_off = 0;
                    if (dim_in_token) {
                        // Emit just "POLY(N)" — drop any glued VS names.
                        new_line.tokens.push_back(
                            line.tokens[value_start].substr(0, close + 1));
                        scan_off = close + 1;  // resume after ')' in same token
                    } else {
                        new_line.tokens.push_back(line.tokens[value_start]);
                        value_start++;
                        scan_tok = value_start;
                        // Separate "(N)" dimension token.
                        if (value_start < line.tokens.size() &&
                            !line.tokens[value_start].empty() &&
                            line.tokens[value_start].front() == '(') {
                            const std::string& dt = line.tokens[value_start];
                            size_t c2 = dt.find(')');
                            if (c2 != std::string::npos && c2 > 1)
                                poly_dim = std::stoi(dt.substr(1, c2 - 1));
                            new_line.tokens.push_back(dt);
                            value_start++;
                            scan_tok = value_start;
                        }
                    }
                    size_t consumed = 0;
                    auto vs_names = extract_node_atoms(line.tokens, scan_tok,
                                                       poly_dim, consumed, scan_off);
                    for (const auto& vs : vs_names)
                        new_line.tokens.push_back(instance_prefix + "." + to_lower(vs));
                    value_start = scan_tok + consumed;
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

    // Collect top-level (global) .model names so the Q-card node/model
    // disambiguation inside subcircuits can recognize models defined outside
    // the subcircuit body (ngspice resolves against the global model table).
    std::unordered_set<std::string> global_model_names;
    for (const auto& line : lines) {
        if (line.tokens.size() >= 2 && to_lower(line.tokens[0]) == ".model")
            global_model_names.insert(to_lower(line.tokens[1]));
    }

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

            // Find subcircuit name: scan from the parameter-section boundary
            // backward, first token without '=' that's a known subcircuit.
            // Tokens at/after PARAMS:/OPTIONAL:/TEXT: are parameter
            // assignments and must never be treated as the subckt name (e.g.
            // a "CMRR" subckt called as "X a b c CMRR PARAMS: CMRR=150 ...").
            std::string subckt_name;
            size_t subckt_pos = 0;
            for (size_t i = x_param_section_start(line.tokens); i > 1; --i) {
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
                                            global_nodes, global_model_names);
            result.insert(result.end(), expanded.begin(), expanded.end());
        } else {
            result.push_back(line);
        }
    }

    return result;
}

} // namespace neospice
