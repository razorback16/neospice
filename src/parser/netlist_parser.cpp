#include "parser/netlist_parser.hpp"
#include "parser/tokenizer.hpp"
#include "parser/subcircuit.hpp"
#include "parser/subcircuit_expand.hpp"
#include "parser/expression.hpp"
#include "parser/model_cards.hpp"
#include "devices/device_registry.hpp"
#include "devices/resistor.hpp"
#include "devices/resistor_model.hpp"
#include "devices/capacitor.hpp"
#include "devices/capacitor_model.hpp"
#include "devices/inductor.hpp"
#include "devices/inductor_model.hpp"
#include "devices/coupled_inductor.hpp"
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include "devices/vcvs.hpp"
#include "devices/vccs.hpp"
#include "devices/ccvs.hpp"
#include "devices/cccs.hpp"
#include "devices/switch.hpp"
#include "devices/vcvs_nonlinear.hpp"
#include "devices/vccs_nonlinear.hpp"
#include "devices/ccvs_nonlinear.hpp"
#include "devices/cccs_nonlinear.hpp"
#include "devices/tline.hpp"
#include "devices/ltra.hpp"
#include "devices/asrc/asrc_device.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace neospice {

namespace {

std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

// Parse a .func directive from a tokenized line and add to func_defs.
// .func name(arg1, arg2, ...) {body}
// The tokenizer splits on whitespace, so tokens will be like:
//   [".func", "name(arg1,arg2,...)", "{body}"] or with spaces in body
//   [".func", "name(arg1,", "arg2,...)", "{body}"]
void parse_func_def(const std::vector<std::string>& tokens,
                    std::unordered_map<std::string, FuncDef>& func_defs) {
    if (tokens.size() < 3) return;

    // Join tokens 1..end to reconstruct the full signature + body string,
    // since the tokenizer may have split the signature across multiple tokens.
    std::string joined;
    for (size_t i = 1; i < tokens.size(); ++i) {
        if (!joined.empty()) joined += ' ';
        joined += tokens[i];
    }

    // Find the function name: everything before '('
    auto paren = joined.find('(');
    if (paren == std::string::npos) return;
    std::string fname = to_lower(joined.substr(0, paren));
    // Trim trailing whitespace from fname
    while (!fname.empty() && std::isspace(static_cast<unsigned char>(fname.back())))
        fname.pop_back();
    if (fname.empty()) return;

    // Find closing ')' for the argument list
    auto close_paren = joined.find(')', paren);
    if (close_paren == std::string::npos) return;

    // Extract argument names
    std::string args_str = joined.substr(paren + 1, close_paren - paren - 1);
    FuncDef def;
    std::istringstream argss(args_str);
    std::string arg;
    while (std::getline(argss, arg, ',')) {
        auto s = arg.find_first_not_of(" \t");
        auto e = arg.find_last_not_of(" \t");
        if (s != std::string::npos)
            def.args.push_back(to_lower(arg.substr(s, e - s + 1)));
    }

    // Body is everything after ')' — skip optional '=' and whitespace
    std::string body = joined.substr(close_paren + 1);
    // Trim leading whitespace
    while (!body.empty() && std::isspace(static_cast<unsigned char>(body.front())))
        body.erase(0, 1);
    // Skip optional '='
    if (!body.empty() && body.front() == '=') {
        body.erase(0, 1);
        while (!body.empty() && std::isspace(static_cast<unsigned char>(body.front())))
            body.erase(0, 1);
    }
    // Strip surrounding braces if present
    if (body.size() >= 2 && body.front() == '{' && body.back() == '}')
        body = body.substr(1, body.size() - 2);
    // Trim whitespace from body
    while (!body.empty() && std::isspace(static_cast<unsigned char>(body.front())))
        body.erase(0, 1);
    while (!body.empty() && std::isspace(static_cast<unsigned char>(body.back())))
        body.pop_back();

    if (body.empty()) return;
    def.body = body;
    func_defs[fname] = std::move(def);
}

// Parse content between parentheses from token list starting at position idx.
// Returns the values as doubles. Advances idx past the closing ')'.
std::vector<double> parse_paren_params(const std::vector<std::string>& tokens,
                                       size_t& idx) {
    // Join tokens from idx onward to find (...) content
    std::string joined;
    for (size_t i = idx; i < tokens.size(); ++i) {
        if (!joined.empty()) joined += ' ';
        joined += tokens[i];
    }

    size_t open = joined.find('(');
    size_t close = joined.find(')');
    if (open == std::string::npos || close == std::string::npos || close <= open) {
        throw ParseError("Missing parentheses in source specification");
    }

    std::string content = joined.substr(open + 1, close - open - 1);

    // Advance idx past the token containing ')'
    size_t chars_consumed = 0;
    for (size_t i = idx; i < tokens.size(); ++i) {
        chars_consumed += tokens[i].size();
        if (i > idx) chars_consumed += 1; // space
        idx = i + 1;
        if (chars_consumed > close) break;
    }

    // Parse the content as space-separated numbers
    std::vector<double> values;
    std::istringstream iss(content);
    std::string tok;
    while (iss >> tok) {
        values.push_back(parse_spice_number(tok));
    }
    return values;
}

// Parse a source line (VSource or ISource) for DC, AC, PULSE, SIN keywords
struct ParsedSourceSpec {
    double dc_val = 0.0;
    double ac_mag = 0.0;
    double ac_phase = 0.0;
    SourceFunction func = SourceFunction::DC;
    PulseParams pulse;
    SinParams sin;
    PwlParams pwl;
    ExpParams exp;
    SffmParams sffm;
    AmParams am;
};

ParsedSourceSpec parse_source_spec(const std::vector<std::string>& tokens, size_t start_idx) {
    ParsedSourceSpec spec;
    size_t i = start_idx;

    while (i < tokens.size()) {
        std::string lower = to_lower(tokens[i]);

        if (lower == "dc") {
            ++i;
            if (i < tokens.size()) {
                spec.dc_val = parse_spice_number(tokens[i]);
                ++i;
            }
        } else if (lower == "ac") {
            ++i;
            if (i < tokens.size()) {
                spec.ac_mag = parse_spice_number(tokens[i]);
                ++i;
            }
            if (i < tokens.size()) {
                std::string next_lower = to_lower(tokens[i]);
                // Check if next token is a number (AC phase) or a keyword
                if (next_lower != "dc" && next_lower != "pulse" && next_lower != "sin" &&
                    next_lower != "pwl" && next_lower != "exp" && next_lower != "sffm" &&
                    next_lower != "am" &&
                    next_lower.find('(') == std::string::npos) {
                    try {
                        spec.ac_phase = parse_spice_number(tokens[i]);
                        ++i;
                    } catch (...) {
                        // Not a number, leave it for next iteration
                    }
                }
            }
        } else if (lower == "pulse" || lower.substr(0, 5) == "pulse") {
            // PULSE(...) — could be "PULSE(..." or "PULSE" "(" ...
            auto vals = parse_paren_params(tokens, i);
            spec.func = SourceFunction::PULSE;
            if (vals.size() >= 1) spec.pulse.v1  = vals[0];
            if (vals.size() >= 2) spec.pulse.v2  = vals[1];
            if (vals.size() >= 3) spec.pulse.td  = vals[2];
            if (vals.size() >= 4) spec.pulse.tr  = vals[3];
            if (vals.size() >= 5) spec.pulse.tf  = vals[4];
            if (vals.size() >= 6) spec.pulse.pw  = vals[5];
            if (vals.size() >= 7) spec.pulse.per = vals[6];
        } else if (lower == "sin" || lower.substr(0, 3) == "sin") {
            auto vals = parse_paren_params(tokens, i);
            spec.func = SourceFunction::SIN;
            if (vals.size() >= 1) spec.sin.v0    = vals[0];
            if (vals.size() >= 2) spec.sin.va    = vals[1];
            if (vals.size() >= 3) spec.sin.freq  = vals[2];
            if (vals.size() >= 4) spec.sin.td    = vals[3];
            if (vals.size() >= 5) spec.sin.theta = vals[4];
            if (vals.size() >= 6) spec.sin.phase = vals[5];
        } else if (lower == "pwl" || lower.substr(0, 3) == "pwl") {
            auto vals = parse_paren_params(tokens, i);
            spec.func = SourceFunction::PWL;
            for (size_t j = 0; j + 1 < vals.size(); j += 2) {
                spec.pwl.points.emplace_back(vals[j], vals[j + 1]);
            }
        } else if (lower == "exp" || lower.substr(0, 3) == "exp") {
            auto vals = parse_paren_params(tokens, i);
            spec.func = SourceFunction::EXP;
            if (vals.size() >= 1) spec.exp.v1   = vals[0];
            if (vals.size() >= 2) spec.exp.v2   = vals[1];
            if (vals.size() >= 3) spec.exp.td1  = vals[2];
            if (vals.size() >= 4) spec.exp.tau1 = vals[3];
            if (vals.size() >= 5) spec.exp.td2  = vals[4];
            if (vals.size() >= 6) spec.exp.tau2 = vals[5];
        } else if (lower == "sffm" || lower.substr(0, 4) == "sffm") {
            auto vals = parse_paren_params(tokens, i);
            spec.func = SourceFunction::SFFM;
            if (vals.size() >= 1) spec.sffm.vo  = vals[0];
            if (vals.size() >= 2) spec.sffm.va  = vals[1];
            if (vals.size() >= 3) spec.sffm.fc  = vals[2];
            if (vals.size() >= 4) spec.sffm.mdi = vals[3];
            if (vals.size() >= 5) spec.sffm.fs  = vals[4];
        } else if (lower == "am" || lower.substr(0, 2) == "am") {
            auto vals = parse_paren_params(tokens, i);
            spec.func = SourceFunction::AM;
            if (vals.size() >= 1) spec.am.sa = vals[0];
            if (vals.size() >= 2) spec.am.oc = vals[1];
            if (vals.size() >= 3) spec.am.fm = vals[2];
            if (vals.size() >= 4) spec.am.fc = vals[3];
            if (vals.size() >= 5) spec.am.td = vals[4];
        } else {
            // Try to parse as a bare DC value (no "DC" keyword)
            try {
                spec.dc_val = parse_spice_number(tokens[i]);
                ++i;
            } catch (...) {
                ++i; // skip unknown token
            }
        }
    }

    return spec;
}

// ---------------------------------------------------------------------------
// extract_lib_section
// ---------------------------------------------------------------------------
// Given the text content of a library file and a section name, return the
// lines that appear between ".lib section_name" and ".endl [section_name]".
// Section name comparison is case-insensitive.
// Throws ParseError if the named section is not found.
// If multiple matching sections exist, the first one is returned.
// A bare ".endl" (no section name) also terminates the current section.
static std::string extract_lib_section(const std::string& content,
                                       const std::string& section) {
    std::string section_lower = to_lower(section);

    std::istringstream input(content);
    std::string line;
    bool in_section = false;
    std::ostringstream result;
    bool found = false;

    while (std::getline(input, line)) {
        // Tokenize the line minimally: split on whitespace
        std::string stripped = line;
        // Find first non-space position
        size_t start = 0;
        while (start < stripped.size() &&
               std::isspace(static_cast<unsigned char>(stripped[start])))
            ++start;

        // Build lowercase version from start
        std::string lower_stripped = stripped.substr(start);
        std::transform(lower_stripped.begin(), lower_stripped.end(),
                       lower_stripped.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        // Tokenize by whitespace to get keyword and args
        std::vector<std::string> toks;
        {
            std::istringstream ss(lower_stripped);
            std::string tok;
            while (ss >> tok) toks.push_back(tok);
        }

        if (toks.empty()) {
            if (in_section) result << line << '\n';
            continue;
        }

        if (toks[0] == ".lib") {
            // ".lib section_name" — section start delimiter (2 tokens)
            if (toks.size() == 2 && toks[1] == section_lower) {
                if (!in_section) {
                    in_section = true;
                    found = true;
                }
                // Skip the delimiter line itself
                continue;
            } else if (toks.size() == 2 && in_section) {
                // A different section start inside our section — treat as content
                // (shouldn't normally happen, but be defensive)
                result << line << '\n';
                continue;
            }
            // Any other .lib form (3+ tokens) while in_section — treat as content
            if (in_section) result << line << '\n';
            continue;
        }

        if (toks[0] == ".endl") {
            if (in_section) {
                // ".endl" with no name, or ".endl section_name" matching ours
                if (toks.size() == 1 ||
                    (toks.size() >= 2 && toks[1] == section_lower)) {
                    // End of our section
                    in_section = false;
                    break; // stop after the first matching section
                } else {
                    // .endl for a different section name — treat as content
                    result << line << '\n';
                }
            }
            continue;
        }

        if (in_section) {
            result << line << '\n';
        }
    }

    if (!found) {
        throw ParseError(".lib: section '" + section + "' not found in library file");
    }

    return result.str();
}

} // anonymous namespace

Circuit NetlistParser::parse(const std::string& netlist) {
    Circuit ckt;
    ParseState state(ckt, subcircuit_defs_);

    // Strip leading whitespace so the tokenizer's title-line detection works
    // correctly even when the netlist starts with newlines.
    size_t start = 0;
    while (start < netlist.size() &&
           std::isspace(static_cast<unsigned char>(netlist[start])))
        ++start;
    std::string trimmed = netlist.substr(start);

    if (dialect_ == SpiceDialect::AUTO)
        dialect_ = detect_dialect(trimmed);

    // Extract the title (first non-empty line) before tokenization discards it.
    {
        auto nl = trimmed.find('\n');
        std::string first_line = (nl != std::string::npos) ? trimmed.substr(0, nl) : trimmed;
        while (!first_line.empty() && (first_line.back() == '\r' || first_line.back() == ' ' || first_line.back() == '\t'))
            first_line.pop_back();
        std::transform(first_line.begin(), first_line.end(), first_line.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        ckt.title = first_line;
    }

    state.lines = tokenize(trimmed);
    state.dialect = dialect_;

    pass0_extract_subcircuits(state);
    pass025_resolve_funcs_params(state);
    pass04_collect_globals(state);
    pass05_expand_subcircuits(state);
    pass1_collect_models_params(state);
    pass2_parse_elements(state);
    pass3_resolve_deferred(state);

    ckt.finalize();
    return ckt;
}

// ---------------------------------------------------------------------------
// Pass 0: Extract .subckt/.ends blocks into subcircuit_defs_
// ---------------------------------------------------------------------------
void NetlistParser::pass0_extract_subcircuits(ParseState& state) {
    {
        state.subcircuit_defs.clear();
        std::vector<TokenizedLine> remaining_lines;
        remaining_lines.reserve(state.lines.size());

        int depth = 0;
        SubcircuitDef current_def;
        // Stack of outer defs when nesting deeper than 1
        std::vector<SubcircuitDef> def_stack;

        for (const auto& line : state.lines) {
            if (line.tokens.empty()) {
                if (depth > 0) {
                    current_def.body.push_back(line);
                } else {
                    remaining_lines.push_back(line);
                }
                continue;
            }

            std::string first = to_lower(line.tokens[0]);

            if (first == ".subckt") {
                if (line.tokens.size() < 2) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .subckt requires a subcircuit name");
                }

                if (depth > 0) {
                    // Nested .subckt — push current onto the stack and start a new one
                    def_stack.push_back(std::move(current_def));
                    current_def = SubcircuitDef{};
                }

                // Parse the .subckt header: .subckt name [port...] [key=val ...]
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
                        // key=value => parameter default
                        std::string key = to_lower(tok.substr(0, eq_pos));
                        std::string val = tok.substr(eq_pos + 1);
                        current_def.default_params.emplace_back(key, val);
                        seen_param = true;
                    } else {
                        // No '=' => port name; error if params already seen
                        if (seen_param) {
                            throw ParseError("Line " + std::to_string(line.line_number) +
                                             ": port '" + tok +
                                             "' appears after parameter defaults in .subckt header");
                        }
                        current_def.ports.push_back(to_lower(tok));
                    }
                }

                depth++;

            } else if (first == ".ends") {
                if (depth == 0) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .ends without matching .subckt");
                }

                depth--;

                if (depth == 0) {
                    // Finished the outermost subcircuit definition
                    state.subcircuit_defs[current_def.name] = std::move(current_def);
                    current_def = SubcircuitDef{};
                } else {
                    // End of a nested subcircuit. Collect the inner def, pop the
                    // outer context from the stack, and store the inner subcircuit
                    // as raw lines in the outer def's body (Task 7.2 will parse them
                    // during expansion).
                    SubcircuitDef inner_def = std::move(current_def);
                    current_def = std::move(def_stack.back());
                    def_stack.pop_back();

                    // Reconstruct the .subckt header token line for the inner def
                    // so the outer body contains a complete, self-contained block.
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

                    // Add inner body lines to outer body
                    for (const auto& bl : inner_def.body) {
                        current_def.body.push_back(bl);
                    }

                    // Add the .ends line to the outer body
                    current_def.body.push_back(line);
                }

            } else {
                // Regular line
                if (depth > 0) {
                    current_def.body.push_back(line);
                } else {
                    remaining_lines.push_back(line);
                }
            }
        }

        if (depth > 0) {
            throw ParseError("Unterminated .subckt '" + current_def.name + "': missing .ends");
        }

        state.lines = std::move(remaining_lines);
    }
}

// ---------------------------------------------------------------------------
// Pass 0.25: Resolve .func and .param definitions
// ---------------------------------------------------------------------------
void NetlistParser::pass025_resolve_funcs_params(ParseState& state) {
    // Pre-collect .func definitions, expand func calls in all
    // tokens, then collect top-level .param entries so that expansion
    // (Pass 0.5) can resolve parameter expressions like R={myR}.
    {
        // First collect .func definitions
        for (const auto& line : state.lines) {
            if (line.tokens.empty()) continue;
            std::string first = to_lower(line.tokens[0]);
            if (first == ".func") {
                parse_func_def(line.tokens, state.func_defs);
            }
        }

        // Expand .func calls in all tokens (modifies lines in place)
        if (!state.func_defs.empty()) {
            for (auto& line : state.lines) {
                for (auto& tok : line.tokens) {
                    tok = expand_funcs(tok, state.func_defs);
                }
            }
        }

        // Then collect .param entries
        std::vector<std::pair<std::string, std::string>> pre_raw_params;
        for (const auto& line : state.lines) {
            if (line.tokens.empty()) continue;
            std::string first = to_lower(line.tokens[0]);
            if (first == ".param") {
                for (size_t i = 1; i < line.tokens.size(); ++i) {
                    auto eq_pos = line.tokens[i].find('=');
                    if (eq_pos != std::string::npos) {
                        std::string key = to_lower(line.tokens[i].substr(0, eq_pos));
                        std::string val_str = line.tokens[i].substr(eq_pos + 1);
                        pre_raw_params.emplace_back(key, val_str);
                    }
                }
            }
        }
        if (!pre_raw_params.empty()) {
            state.global_params = resolve_params(pre_raw_params);
        }
    }
}

// ---------------------------------------------------------------------------
// Pass 0.4: Collect .global node declarations
// ---------------------------------------------------------------------------
void NetlistParser::pass04_collect_globals(ParseState& state) {
    // Nodes listed on .global lines are shared across all subcircuit
    // hierarchies and must NOT be prefixed during expansion.
    for (const auto& line : state.lines) {
        if (line.tokens.empty()) continue;
        if (to_lower(line.tokens[0]) == ".global") {
            for (size_t i = 1; i < line.tokens.size(); ++i) {
                state.global_nodes.insert(to_lower(line.tokens[i]));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Pass 0.5: Expand subcircuit instances
// ---------------------------------------------------------------------------
void NetlistParser::pass05_expand_subcircuits(ParseState& state) {
    // Expand X instances into primitive element lines.
    // After this step, `lines` contains no X elements.
    // Always call — even when subcircuit_defs is empty — so that X lines
    // referencing undefined subcircuits produce a proper ParseError.
    state.lines = expand_all_instances(state.lines, state.subcircuit_defs,
                                       state.global_params, state.global_nodes);
}

// ---------------------------------------------------------------------------
// Pass 1: Collect .model, .func, and .param cards
// ---------------------------------------------------------------------------
void NetlistParser::pass1_collect_models_params(ParseState& state) {
    // Re-collect .func from post-expansion lines (subcircuit bodies may define .func)
    for (const auto& line : state.lines) {
        if (line.tokens.empty()) continue;
        std::string first = to_lower(line.tokens[0]);
        if (first == ".func") {
            parse_func_def(line.tokens, state.func_defs);
        }
    }

    // Expand .func calls in all tokens (post-subcircuit-expansion lines)
    if (!state.func_defs.empty()) {
        for (auto& line : state.lines) {
            for (auto& tok : line.tokens) {
                tok = expand_funcs(tok, state.func_defs);
            }
        }
    }

    std::vector<std::pair<std::string, std::string>> raw_params;
    for (const auto& line : state.lines) {
        if (line.tokens.empty()) continue;
        std::string first = to_lower(line.tokens[0]);

        if (first == ".model") {
            auto card = parse_model_card(line.tokens);
            state.models[card.name] = card;
            if (card.type == "r") {
                state.res_models[to_lower(card.name)] = to_resistor_model(card);
            } else if (card.type == "c") {
                state.cap_models[to_lower(card.name)] = to_capacitor_model(card);
            } else if (card.type == "l") {
                state.ind_models[to_lower(card.name)] = to_inductor_model(card);
            }
        } else if (first == ".param") {
            // .param key=value  or  .param key={expr}
            // Collect raw (name, expression) pairs; resolve later in dependency order.
            for (size_t i = 1; i < line.tokens.size(); ++i) {
                auto eq_pos = line.tokens[i].find('=');
                if (eq_pos != std::string::npos) {
                    std::string key = line.tokens[i].substr(0, eq_pos);
                    std::string val_str = line.tokens[i].substr(eq_pos + 1);
                    raw_params.emplace_back(key, val_str);
                }
            }
        }
    }
    // Resolve PSpice AKO: (A Kind Of) model inheritance.
    // Multiple passes handle chains (A inherits B, B inherits C).
    {
        // Track which models still need resolution
        bool changed = true;
        int max_passes = static_cast<int>(state.models.size()) + 1; // detect cycles
        int pass = 0;
        while (changed && pass < max_passes) {
            changed = false;
            ++pass;
            for (auto& [name, card] : state.models) {
                if (card.ako_base.empty()) continue;
                auto base_it = state.models.find(card.ako_base);
                if (base_it == state.models.end()) {
                    throw ParseError(".model " + name + " AKO: base model '"
                                     + card.ako_base + "' not found");
                }
                const auto& base = base_it->second;
                // If base itself has unresolved AKO, defer to next pass
                if (!base.ako_base.empty()) continue;
                // Inherit type if derived doesn't specify one
                if (card.type.empty()) {
                    card.type = base.type;
                }
                // Merge params: start with base, overlay derived
                auto merged = base.params;
                for (const auto& [pk, pv] : card.params) {
                    merged[pk] = pv;
                }
                card.params = std::move(merged);
                // Mark as resolved
                card.ako_base.clear();
                changed = true;
                // Update typed model maps
                if (card.type == "r") {
                    state.res_models[to_lower(name)] = to_resistor_model(card);
                } else if (card.type == "c") {
                    state.cap_models[to_lower(name)] = to_capacitor_model(card);
                } else if (card.type == "l") {
                    state.ind_models[to_lower(name)] = to_inductor_model(card);
                }
            }
        }
        // If any AKO still unresolved after max passes, there's a cycle
        for (const auto& [name, card] : state.models) {
            if (!card.ako_base.empty()) {
                throw ParseError(".model " + name + " AKO: circular inheritance detected");
            }
        }
    }

    // Resolve all .param definitions in dependency order (handles forward references)
    if (!raw_params.empty()) {
        state.params = resolve_params(raw_params);
    }
}

// ---------------------------------------------------------------------------
// Pass 2: Parse element lines and dot commands
// ---------------------------------------------------------------------------
void NetlistParser::pass2_parse_elements(ParseState& state) {
    auto& ckt = state.ckt;
    auto& node_raw = state.node_raw;
    auto& func_defs = state.func_defs;
    auto& res_models = state.res_models;
    auto& cap_models = state.cap_models;
    auto& ind_models = state.ind_models;
    auto& parsed_elements = state.parsed_elements;
    auto& models = state.models;
    auto& deferred_ccvs = state.deferred_ccvs;
    auto& deferred_cccs = state.deferred_cccs;
    auto& deferred_poly_ccvs = state.deferred_poly_ccvs;
    auto& deferred_poly_cccs = state.deferred_poly_cccs;
    auto& deferred_coupled_inductors = state.deferred_coupled_inductors;
    auto& deferred_vswitches = state.deferred_vswitches;
    auto& deferred_cswitches = state.deferred_cswitches;
    auto& deferred_ltras = state.deferred_ltras;
    auto& deferred_asrcs = state.deferred_asrcs;

    using DeferredCCVS = ParseState::DeferredCCVS;
    using DeferredCCCS = ParseState::DeferredCCCS;
    using DeferredPolyCCVS = ParseState::DeferredPolyCCVS;
    using DeferredPolyCCCS = ParseState::DeferredPolyCCCS;
    using DeferredCoupledInductor = ParseState::DeferredCoupledInductor;
    using DeferredVSwitch = ParseState::DeferredVSwitch;
    using DeferredCSwitch = ParseState::DeferredCSwitch;
    using DeferredLTRA = ParseState::DeferredLTRA;
    using DeferredASRC = ParseState::DeferredASRC;

    for (const auto& line : state.lines) {
        if (line.tokens.empty()) continue;
        const auto& tokens = line.tokens;
        std::string first = to_lower(tokens[0]);

        // Dot commands
        if (first[0] == '.') {
            if (first == ".op") {
                ckt.analyses.push_back(OpCmd{});
            } else if (first == ".tran") {
                if (tokens.size() < 3) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .tran requires tstep and tstop");
                }
                TranCmd tran;
                tran.tstep = parse_spice_number(tokens[1]);
                tran.tstop = parse_spice_number(tokens[2]);
                // Check remaining tokens for UIC keyword
                for (size_t k = 3; k < tokens.size(); ++k) {
                    if (to_lower(tokens[k]) == "uic") {
                        tran.uic = true;
                    }
                }
                ckt.analyses.push_back(tran);
            } else if (first == ".ac") {
                if (tokens.size() < 5) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .ac requires mode npoints fstart fstop");
                }
                ACCmd ac;
                std::string mode = to_lower(tokens[1]);
                if (mode == "dec") ac.mode = ACMode::DEC;
                else if (mode == "oct") ac.mode = ACMode::OCT;
                else if (mode == "lin") ac.mode = ACMode::LIN;
                else throw ParseError("Unknown AC mode: " + tokens[1]);
                ac.npoints = static_cast<int>(parse_spice_number(tokens[2]));
                ac.fstart = parse_spice_number(tokens[3]);
                ac.fstop = parse_spice_number(tokens[4]);
                ckt.analyses.push_back(ac);
            } else if (first == ".noise") {
                // .noise V(out) Vin dec 10 1 1e9
                if (tokens.size() < 7) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .noise requires V(output) input_source mode npoints fstart fstop");
                }
                NoiseCmd ncmd;

                // Parse output spec: V(node) or v(node)
                std::string out_spec = tokens[1];
                std::string out_lower = to_lower(out_spec);
                if (out_lower.size() > 3 && out_lower.substr(0, 2) == "v(" && out_lower.back() == ')') {
                    ncmd.output = out_lower.substr(2, out_lower.size() - 3);
                } else {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .noise output must be V(node)");
                }

                // Input source name
                ncmd.input_src = tokens[2];

                // Frequency sweep parameters
                std::string mode = to_lower(tokens[3]);
                if (mode == "dec") ncmd.mode = ACMode::DEC;
                else if (mode == "oct") ncmd.mode = ACMode::OCT;
                else if (mode == "lin") ncmd.mode = ACMode::LIN;
                else throw ParseError("Line " + std::to_string(line.line_number) +
                                      ": Unknown noise sweep mode: " + tokens[3]);
                ncmd.npoints = static_cast<int>(parse_spice_number(tokens[4]));
                ncmd.fstart = parse_spice_number(tokens[5]);
                ncmd.fstop = parse_spice_number(tokens[6]);
                ckt.analyses.push_back(ncmd);
            } else if (first == ".dc") {
                // .dc Vsrc1 start1 stop1 step1 [Vsrc2 start2 stop2 step2]
                // Each sweep group is 4 tokens: source_name start stop step
                if (tokens.size() < 5) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .dc requires source start stop step");
                }
                DCSweepCmd dcmd;
                size_t i = 1;
                while (i + 3 < tokens.size() + 1 && i < tokens.size()) {
                    // Need 4 tokens: name start stop step
                    if (i + 3 >= tokens.size() + 1) break;
                    if (i + 3 > tokens.size()) break;
                    DCSweepParam sp;
                    sp.source_name = tokens[i];
                    try {
                        sp.start = parse_spice_number(tokens[i + 1]);
                        sp.stop  = parse_spice_number(tokens[i + 2]);
                        sp.step  = parse_spice_number(tokens[i + 3]);
                    } catch (...) {
                        throw ParseError("Line " + std::to_string(line.line_number) +
                                         ": .dc sweep parameters must be numbers");
                    }
                    dcmd.params.push_back(sp);
                    i += 4;
                    if (dcmd.params.size() >= 2) break; // support at most 2 sweeps
                }
                if (dcmd.params.empty()) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .dc requires at least one sweep group");
                }
                ckt.analyses.push_back(dcmd);
            } else if (first == ".options") {
                for (size_t i = 1; i < tokens.size(); ++i) {
                    auto eq_pos = tokens[i].find('=');
                    if (eq_pos == std::string::npos) {
                        // Bare flag options (no '=' sign)
                        std::string flag = to_lower(tokens[i]);
                        if (flag == "interp") ckt.options.interp = true;
                        continue;
                    }
                    std::string key = to_lower(tokens[i].substr(0, eq_pos));
                    std::string val_str = tokens[i].substr(eq_pos + 1);
                    // method is a string option; all others are numeric
                    if (key == "method") {
                        ckt.options.method = to_lower(val_str);
                    } else {
                        double val = parse_spice_number(val_str);
                        if      (key == "reltol") ckt.options.reltol = val;
                        else if (key == "abstol") ckt.options.abstol = val;
                        else if (key == "vntol")  ckt.options.vntol  = val;
                        else if (key == "gmin")   ckt.options.gmin   = val;
                        else if (key == "trtol")  ckt.options.trtol  = val;
                        else if (key == "chgtol") ckt.options.chgtol = val;
                        else if (key == "temp") {
                            ckt.options.temp = val + 273.15;
                        } else if (key == "tnom") {
                            ckt.options.tnom = val + 273.15;
                        } else if (key == "itl1") {
                            ckt.options.itl1 = static_cast<int>(val);
                            ckt.options.max_iter = ckt.options.itl1;
                        } else if (key == "itl4") {
                            ckt.options.itl4 = static_cast<int>(val);
                        } else if (key == "lte_ref_mode") {
                            ckt.options.lte_ref_mode = static_cast<int>(val);
                        } else if (key == "restart_step_scale") {
                            ckt.options.restart_step_scale = val;
                        }
                        // Silently ignore unrecognised option keys
                    }
                }
            } else if (first == ".ic") {
                // .ic V(node)=value ...
                for (size_t i = 1; i < tokens.size(); ++i) {
                    std::string tok = tokens[i];
                    // Format: V(nodename)=value
                    auto eq_pos = tok.find('=');
                    if (eq_pos == std::string::npos) continue;
                    std::string lhs = tok.substr(0, eq_pos);
                    double val = parse_spice_number(tok.substr(eq_pos + 1));
                    // Extract node name from V(name)
                    std::string llhs = to_lower(lhs);
                    if (llhs.size() > 3 && llhs.substr(0, 2) == "v(" && llhs.back() == ')') {
                        std::string node_name = lhs.substr(2, lhs.size() - 3);
                        NodeId idx = ckt.node(node_name);
                        ckt.ic[idx] = val;
                    }
                }
            } else if (first == ".nodeset") {
                for (size_t i = 1; i < tokens.size(); ++i) {
                    std::string tok = tokens[i];
                    auto eq_pos = tok.find('=');
                    if (eq_pos == std::string::npos) continue;
                    std::string lhs = tok.substr(0, eq_pos);
                    double val = parse_spice_number(tok.substr(eq_pos + 1));
                    std::string llhs = to_lower(lhs);
                    if (llhs.size() > 3 && llhs.substr(0, 2) == "v(" && llhs.back() == ')') {
                        std::string node_name = lhs.substr(2, lhs.size() - 3);
                        NodeId idx = ckt.node(node_name);
                        ckt.nodeset[idx] = val;
                    }
                }
            } else if (first == ".save") {
                // .save V(out) I(V1) ...
                for (size_t i = 1; i < tokens.size(); ++i) {
                    std::string sig = to_lower(tokens[i]);
                    if (sig.empty()) continue;
                    // Normalize branch current names to ngspice format:
                    // i(x1.ehf) → i(e.x1.ehf) for hierarchical devices
                    if (sig.size() > 3 && sig[0] == 'i' && sig[1] == '(') {
                        std::string inner = sig.substr(2, sig.size() - 3);
                        auto dot = inner.rfind('.');
                        if (dot != std::string::npos && dot + 1 < inner.size()) {
                            char tl = inner[dot + 1];
                            // Only normalize if not already in ngspice format
                            // (ngspice format has type letter, dot, then subckt path)
                            if (inner.find('.') != 1)
                                sig = "i(" + std::string(1, tl) + "." + inner + ")";
                        }
                    }
                    ckt.save_signals.push_back(sig);
                }
            } else if (first == ".meas" || first == ".measure") {
                // .meas analysis_type name measure_spec...
                if (tokens.size() < 4) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .meas requires analysis_type, name, and measure specification");
                }
                MeasureCommand mcmd;
                mcmd.analysis_type = to_lower(tokens[1]);
                mcmd.name = to_lower(tokens[2]);

                // Determine measure type from token 3+
                // Handle case where token 3 might be PARAM='expr' (keyword=value form)
                std::string tok3_full = to_lower(tokens[3]);
                std::string tok3 = tok3_full;
                auto tok3_eq = tok3_full.find('=');
                if (tok3_eq != std::string::npos) {
                    tok3 = tok3_full.substr(0, tok3_eq);
                }

                if (tok3 == "avg" || tok3 == "rms" || tok3 == "min" ||
                    tok3 == "max" || tok3 == "pp" || tok3 == "integ") {
                    // Statistical measure: .meas type name FUNC signal [FROM=x TO=y]
                    mcmd.measure_type = tok3;
                    if (tokens.size() < 5) {
                        throw ParseError("Line " + std::to_string(line.line_number) +
                                         ": .meas " + tok3 + " requires a signal name");
                    }
                    mcmd.signal = to_lower(tokens[4]);
                    // Parse optional FROM= and TO=
                    for (size_t i = 5; i < tokens.size(); ++i) {
                        auto eq_pos = tokens[i].find('=');
                        if (eq_pos == std::string::npos) continue;
                        std::string key = to_lower(tokens[i].substr(0, eq_pos));
                        double val = parse_spice_number(tokens[i].substr(eq_pos + 1));
                        if (key == "from") mcmd.from_val = val;
                        else if (key == "to") mcmd.to_val = val;
                    }
                } else if (tok3 == "trig") {
                    // TRIG/TARG: .meas type name TRIG signal VAL=v RISE=n|FALL=n|CROSS=n
                    //                            TARG signal VAL=v RISE=n|FALL=n|CROSS=n
                    mcmd.measure_type = "trig_targ";
                    // Parse TRIG section
                    size_t i = 4;
                    if (i >= tokens.size()) {
                        throw ParseError("Line " + std::to_string(line.line_number) +
                                         ": .meas TRIG requires a signal name");
                    }
                    mcmd.trig_signal = to_lower(tokens[i]);
                    ++i;
                    // Parse VAL=, RISE=, FALL=, CROSS= until we hit TARG
                    for (; i < tokens.size(); ++i) {
                        std::string lower = to_lower(tokens[i]);
                        if (lower == "targ") break;
                        auto eq_pos = lower.find('=');
                        if (eq_pos == std::string::npos) continue;
                        std::string key = lower.substr(0, eq_pos);
                        std::string valstr = tokens[i].substr(eq_pos + 1);
                        if (key == "val") mcmd.trig_val = parse_spice_number(valstr);
                        else if (key == "rise") { mcmd.trig_direction = "rise"; mcmd.trig_td_count = static_cast<int>(parse_spice_number(valstr)); }
                        else if (key == "fall") { mcmd.trig_direction = "fall"; mcmd.trig_td_count = static_cast<int>(parse_spice_number(valstr)); }
                        else if (key == "cross") { mcmd.trig_direction = "cross"; mcmd.trig_td_count = static_cast<int>(parse_spice_number(valstr)); }
                    }
                    // Parse TARG section
                    if (i < tokens.size() && to_lower(tokens[i]) == "targ") {
                        ++i;
                        if (i >= tokens.size()) {
                            throw ParseError("Line " + std::to_string(line.line_number) +
                                             ": .meas TARG requires a signal name");
                        }
                        mcmd.targ_signal = to_lower(tokens[i]);
                        ++i;
                        for (; i < tokens.size(); ++i) {
                            std::string lower = to_lower(tokens[i]);
                            auto eq_pos = lower.find('=');
                            if (eq_pos == std::string::npos) continue;
                            std::string key = lower.substr(0, eq_pos);
                            std::string valstr = tokens[i].substr(eq_pos + 1);
                            if (key == "val") mcmd.targ_val = parse_spice_number(valstr);
                            else if (key == "rise") { mcmd.targ_direction = "rise"; mcmd.targ_td_count = static_cast<int>(parse_spice_number(valstr)); }
                            else if (key == "fall") { mcmd.targ_direction = "fall"; mcmd.targ_td_count = static_cast<int>(parse_spice_number(valstr)); }
                            else if (key == "cross") { mcmd.targ_direction = "cross"; mcmd.targ_td_count = static_cast<int>(parse_spice_number(valstr)); }
                        }
                    } else {
                        throw ParseError("Line " + std::to_string(line.line_number) +
                                         ": .meas TRIG/TARG missing TARG keyword");
                    }
                } else if (tok3 == "find") {
                    // FIND/WHEN: .meas type name FIND signal WHEN signal2=val [RISE=n|FALL=n]
                    // or:        .meas type name FIND signal AT=val
                    mcmd.measure_type = "find_when";
                    if (tokens.size() < 5) {
                        throw ParseError("Line " + std::to_string(line.line_number) +
                                         ": .meas FIND requires a signal name");
                    }
                    mcmd.find_signal = to_lower(tokens[4]);
                    size_t i = 5;
                    // Look for WHEN or AT=
                    for (; i < tokens.size(); ++i) {
                        std::string lower = to_lower(tokens[i]);
                        auto eq_pos = lower.find('=');
                        if (eq_pos != std::string::npos) {
                            std::string key = lower.substr(0, eq_pos);
                            std::string valstr = tokens[i].substr(eq_pos + 1);
                            if (key == "at") {
                                mcmd.at_given = true;
                                mcmd.at_val = parse_spice_number(valstr);
                            } else if (key == "rise") {
                                mcmd.when_direction = "rise";
                                mcmd.when_td_count = static_cast<int>(parse_spice_number(valstr));
                            } else if (key == "fall") {
                                mcmd.when_direction = "fall";
                                mcmd.when_td_count = static_cast<int>(parse_spice_number(valstr));
                            } else if (key == "cross") {
                                mcmd.when_direction = "cross";
                                mcmd.when_td_count = static_cast<int>(parse_spice_number(valstr));
                            }
                        } else if (lower == "when") {
                            // WHEN signal2=val
                            ++i;
                            if (i >= tokens.size()) {
                                throw ParseError("Line " + std::to_string(line.line_number) +
                                                 ": .meas WHEN requires condition");
                            }
                            // The next token should be signal2=val
                            std::string cond = tokens[i];
                            auto cond_eq = cond.find('=');
                            if (cond_eq != std::string::npos) {
                                mcmd.when_signal = to_lower(cond.substr(0, cond_eq));
                                mcmd.when_val = parse_spice_number(cond.substr(cond_eq + 1));
                            }
                            // Default direction is cross if not specified
                            if (mcmd.when_direction.empty()) mcmd.when_direction = "cross";
                            // Continue parsing for RISE=/FALL=/CROSS=
                        }
                    }
                    // Default when_direction to "cross" if not set
                    if (mcmd.when_direction.empty() && !mcmd.at_given)
                        mcmd.when_direction = "cross";
                } else if (tok3 == "param") {
                    // PARAM: .meas type name PARAM='expression'
                    // or:    .meas type name PARAM 'expression'
                    mcmd.measure_type = "param";
                    std::string expr;
                    if (tok3_eq != std::string::npos) {
                        // Token 3 is PARAM='expr' — extract the expression after '='
                        expr = tokens[3].substr(tok3_eq + 1);
                        for (size_t i = 4; i < tokens.size(); ++i) {
                            expr += ' ';
                            expr += tokens[i];
                        }
                    } else {
                        // Token 3 is just "PARAM", expression starts at token 4+
                        for (size_t i = 4; i < tokens.size(); ++i) {
                            if (!expr.empty()) expr += ' ';
                            expr += tokens[i];
                        }
                    }
                    // Strip surrounding quotes (single or double)
                    if (expr.size() >= 2 &&
                        ((expr.front() == '\'' && expr.back() == '\'') ||
                         (expr.front() == '"' && expr.back() == '"'))) {
                        expr = expr.substr(1, expr.size() - 2);
                    }
                    mcmd.param_expr = expr;
                } else {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": Unknown .meas type: " + tokens[3]);
                }
                ckt.measures.push_back(std::move(mcmd));
            } else if (first == ".print" || first == ".plot") {
                // .print tran V(out) V(in) I(V1)
                // .plot  tran V(out) I(V1)
                if (tokens.size() >= 2) {
                    PrintCommand pcmd;
                    pcmd.analysis_type = to_lower(tokens[1]);
                    pcmd.is_plot = (first == ".plot");
                    for (size_t i = 2; i < tokens.size(); ++i) {
                        if (!tokens[i].empty())
                            pcmd.signals.push_back(to_lower(tokens[i]));
                    }
                    ckt.prints.push_back(std::move(pcmd));
                }
            } else if (first == ".tf") {
                // .tf output_var input_src
                // e.g., .tf V(out) V1  or  .tf I(Vout) V1
                if (tokens.size() < 3) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .tf requires output variable and input source");
                }
                TFCmd tf;
                tf.output = to_lower(tokens[1]);
                tf.input_src = to_lower(tokens[2]);
                ckt.analyses.push_back(tf);
            } else if (first == ".sens") {
                // .sens output_var
                // e.g., .sens V(out)
                if (tokens.size() < 2) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .sens requires output variable");
                }
                SensCmd sens;
                sens.output = to_lower(tokens[1]);
                ckt.analyses.push_back(sens);
            } else if (first == ".pz") {
                // .pz node1 node2 node3 node4 VOL|CUR POL|ZER|PZ
                if (tokens.size() < 7) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .pz requires 6 arguments: n1 n2 n3 n4 VOL|CUR POL|ZER|PZ");
                }
                PZCmd pzcmd;
                pzcmd.in_pos  = to_lower(tokens[1]);
                pzcmd.in_neg  = to_lower(tokens[2]);
                pzcmd.out_pos = to_lower(tokens[3]);
                pzcmd.out_neg = to_lower(tokens[4]);
                std::string transfer = to_lower(tokens[5]);
                if (transfer == "vol")
                    pzcmd.transfer = PZTransferType::VOLTAGE;
                else if (transfer == "cur")
                    pzcmd.transfer = PZTransferType::CURRENT;
                else
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .pz transfer type must be VOL or CUR");
                std::string pz = to_lower(tokens[6]);
                if (pz == "pol")      pzcmd.type = PZType::POLES;
                else if (pz == "zer") pzcmd.type = PZType::ZEROS;
                else if (pz == "pz")  pzcmd.type = PZType::BOTH;
                else
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .pz type must be POL, ZER, or PZ");
                ckt.analyses.push_back(pzcmd);
            } else if (first == ".four" || first == ".fourier") {
                // .four freq signal1 [signal2 ...]
                if (tokens.size() >= 3) {
                    FourierCommand fcmd;
                    fcmd.fundamental_freq = parse_spice_number(tokens[1]);
                    for (size_t i = 2; i < tokens.size(); ++i) {
                        if (!tokens[i].empty())
                            fcmd.signals.push_back(to_lower(tokens[i]));
                    }
                    ckt.fourier_commands.push_back(std::move(fcmd));
                }
            } else if (first == ".step") {
                // .step param <name> <start> <stop> <step>
                // .step <Vsource> <start> <stop> <step>
                // .step temp <start> <stop> <step>
                if (tokens.size() < 5) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .step requires at least 4 arguments");
                }
                StepCommand sc;
                std::string kind_or_name = to_lower(tokens[1]);
                if (kind_or_name == "param") {
                    sc.kind = StepCommand::PARAM;
                    if (tokens.size() < 6) {
                        throw ParseError("Line " + std::to_string(line.line_number) +
                                         ": .step param requires name start stop step");
                    }
                    sc.name  = to_lower(tokens[2]);
                    sc.start = parse_spice_number(tokens[3]);
                    sc.stop  = parse_spice_number(tokens[4]);
                    sc.step  = parse_spice_number(tokens[5]);
                } else if (kind_or_name == "temp") {
                    sc.kind  = StepCommand::TEMP;
                    sc.start = parse_spice_number(tokens[2]);
                    sc.stop  = parse_spice_number(tokens[3]);
                    sc.step  = parse_spice_number(tokens[4]);
                } else {
                    sc.kind = StepCommand::SOURCE;
                    sc.name  = to_lower(tokens[1]);
                    sc.start = parse_spice_number(tokens[2]);
                    sc.stop  = parse_spice_number(tokens[3]);
                    sc.step  = parse_spice_number(tokens[4]);
                }
                ckt.step_commands.push_back(sc);
            }
            // Skip .model, .param (already handled), .include, .lib, .endl, etc.
            continue;
        }

        // Element lines — dispatch by first character of the device name.
        // For hierarchical names from subcircuit expansion (e.g., "x1.r1"),
        // extract the element type from the leaf component (after last '.').
        char elem_type;
        {
            auto dot_pos = first.rfind('.');
            if (dot_pos != std::string::npos && dot_pos + 1 < first.size()) {
                elem_type = first[dot_pos + 1];
            } else {
                elem_type = std::tolower(static_cast<unsigned char>(first[0]));
            }
        }

        if (elem_type == 'r') {
            // R name n+ n- value [model] [TC1=val] [TC2=val] [SCALE=val] [TEMP=val] [DTEMP=val]
            if (tokens.size() < 4) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": Resistor requires name, n+, n-, value");
            }
            std::string name = tokens[0];
            int32_t np = node_raw(tokens[1]);
            int32_t nn = node_raw(tokens[2]);
            double val = parse_spice_number(tokens[3]);
            auto r = std::make_unique<Resistor>(name, np, nn, val);

            // First pass: apply model defaults (if model reference present)
            for (size_t k = 4; k < tokens.size(); ++k) {
                std::string tok_lower = to_lower(tokens[k]);
                if (tok_lower.find('=') == std::string::npos) {
                    auto mit = res_models.find(tok_lower);
                    if (mit != res_models.end()) {
                        const auto& rm = mit->second;
                        r->set_tc1(rm.tc1);
                        r->set_tc2(rm.tc2);
                        if (rm.rac > 0.0) r->set_rac(rm.rac);
                        if (rm.kf != 0.0) r->noise_kf = rm.kf;
                        if (rm.af != 1.0) r->noise_af = rm.af;
                    }
                }
            }

            // Second pass: apply instance params (override model)
            for (size_t k = 4; k < tokens.size(); ++k) {
                std::string tok = to_lower(tokens[k]);
                if (tok.starts_with("tc1="))
                    r->set_tc1(parse_spice_number(tok.substr(4)));
                else if (tok.starts_with("tc2="))
                    r->set_tc2(parse_spice_number(tok.substr(4)));
                else if (tok.starts_with("scale="))
                    r->set_scale(parse_spice_number(tok.substr(6)));
                else if (tok.starts_with("m="))
                    r->set_multiplier(parse_spice_number(tok.substr(2)));
                else if (tok.starts_with("temp="))
                    r->set_temp(parse_spice_number(tok.substr(5)) + 273.15);
                else if (tok.starts_with("dtemp="))
                    r->set_dtemp(parse_spice_number(tok.substr(6)));
                else if (tok.starts_with("rac="))
                    r->set_rac(parse_spice_number(tok.substr(4)));
            }
            ckt.add_device(std::move(r));

        } else if (elem_type == 'c') {
            // C name n+ n- value [model] [TC1=val] [TC2=val] [SCALE=val] [TEMP=val] [DTEMP=val]
            if (tokens.size() < 4) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": Capacitor requires name, n+, n-, value");
            }
            std::string name = tokens[0];
            int32_t np = node_raw(tokens[1]);
            int32_t nn = node_raw(tokens[2]);
            double val = parse_spice_number(tokens[3]);
            auto c = std::make_unique<Capacitor>(name, np, nn, val);

            // First pass: apply model defaults (if model reference present)
            for (size_t k = 4; k < tokens.size(); ++k) {
                std::string tok_lower = to_lower(tokens[k]);
                if (tok_lower.find('=') == std::string::npos) {
                    auto mit = cap_models.find(tok_lower);
                    if (mit != cap_models.end()) {
                        const auto& cm = mit->second;
                        c->set_tc1(cm.tc1);
                        c->set_tc2(cm.tc2);
                    }
                }
            }

            // Second pass: apply instance params (override model)
            for (size_t k = 4; k < tokens.size(); ++k) {
                std::string tok = to_lower(tokens[k]);
                if (tok.starts_with("tc1="))
                    c->set_tc1(parse_spice_number(tok.substr(4)));
                else if (tok.starts_with("tc2="))
                    c->set_tc2(parse_spice_number(tok.substr(4)));
                else if (tok.starts_with("scale="))
                    c->set_scale(parse_spice_number(tok.substr(6)));
                else if (tok.starts_with("m="))
                    c->set_multiplier(parse_spice_number(tok.substr(2)));
                else if (tok.starts_with("temp="))
                    c->set_temp(parse_spice_number(tok.substr(5)) + 273.15);
                else if (tok.starts_with("dtemp="))
                    c->set_dtemp(parse_spice_number(tok.substr(6)));
                else if (tok.starts_with("ic="))
                    c->set_ic(parse_spice_number(tok.substr(3)));
            }
            ckt.add_device(std::move(c));

        } else if (elem_type == 'l') {
            // L name n+ n- [model] value [TC1=val] [TC2=val] [SCALE=val] [TEMP=val] [DTEMP=val]
            // ngspice allows:  L name n+ n- model value   (model before value)
            //                  L name n+ n- value          (no model)
            if (tokens.size() < 4) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": Inductor requires name, n+, n-, value");
            }
            std::string name = tokens[0];
            int32_t np = node_raw(tokens[1]);
            int32_t nn = node_raw(tokens[2]);

            // Detect whether tokens[3] is a model name or a numeric value.
            // If it looks like a model name (found in ind_models), the value
            // is the next token; otherwise tokens[3] is the value.
            double val;
            size_t param_start;
            std::string tok3_lower = to_lower(tokens[3]);
            auto model_it = ind_models.find(tok3_lower);
            if (model_it != ind_models.end()) {
                // tokens[3] is a model name
                if (tokens.size() < 5) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": Inductor with model requires a value");
                }
                val = parse_spice_number(tokens[4]);
                param_start = 5;
            } else {
                val = parse_spice_number(tokens[3]);
                param_start = 4;
            }
            auto l = std::make_unique<Inductor>(name, np, nn, val);

            // First pass: apply model defaults (if model reference present)
            // Check tokens[3] (already checked above) and also scan remaining
            // tokens for bare model names.
            if (model_it != ind_models.end()) {
                const auto& im = model_it->second;
                l->set_tc1(im.tc1);
                l->set_tc2(im.tc2);
            }
            for (size_t k = param_start; k < tokens.size(); ++k) {
                std::string tok_lower = to_lower(tokens[k]);
                if (tok_lower.find('=') == std::string::npos) {
                    auto mit = ind_models.find(tok_lower);
                    if (mit != ind_models.end()) {
                        const auto& im = mit->second;
                        l->set_tc1(im.tc1);
                        l->set_tc2(im.tc2);
                    }
                }
            }

            // Second pass: apply instance params (override model)
            for (size_t k = param_start; k < tokens.size(); ++k) {
                std::string tok = to_lower(tokens[k]);
                if (tok.starts_with("tc1="))
                    l->set_tc1(parse_spice_number(tok.substr(4)));
                else if (tok.starts_with("tc2="))
                    l->set_tc2(parse_spice_number(tok.substr(4)));
                else if (tok.starts_with("scale="))
                    l->set_scale(parse_spice_number(tok.substr(6)));
                else if (tok.starts_with("m="))
                    l->set_multiplier(parse_spice_number(tok.substr(2)));
                else if (tok.starts_with("temp="))
                    l->set_temp(parse_spice_number(tok.substr(5)) + 273.15);
                else if (tok.starts_with("dtemp="))
                    l->set_dtemp(parse_spice_number(tok.substr(6)));
                else if (tok.starts_with("ic="))
                    l->set_ic(parse_spice_number(tok.substr(3)));
            }
            ckt.add_device(std::move(l));

        } else if (elem_type == 'v') {
            // V name n+ n- [DC val] [AC mag [phase]] [PULSE(...)] [SIN(...)]
            if (tokens.size() < 3) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": VSource requires name, n+, n-");
            }
            std::string name = tokens[0];
            int32_t np = node_raw(tokens[1]);
            int32_t nn = node_raw(tokens[2]);

            ParsedSourceSpec spec = parse_source_spec(tokens, 3);
            auto vs = std::make_unique<VSource>(name, np, nn, spec.dc_val);
            if (spec.ac_mag != 0.0 || spec.ac_phase != 0.0) {
                vs->set_ac(spec.ac_mag, spec.ac_phase);
            }
            if (spec.func == SourceFunction::PULSE) vs->set_pulse(spec.pulse);
            else if (spec.func == SourceFunction::SIN) vs->set_sin(spec.sin);
            else if (spec.func == SourceFunction::PWL) vs->set_pwl(spec.pwl);
            else if (spec.func == SourceFunction::EXP) vs->set_exp(spec.exp);
            else if (spec.func == SourceFunction::SFFM) vs->set_sffm(spec.sffm);
            else if (spec.func == SourceFunction::AM) vs->set_am(spec.am);
            ckt.add_device(std::move(vs));

        } else if (elem_type == 'i') {
            if (tokens.size() < 3) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": ISource requires name, n+, n-");
            }
            std::string name = tokens[0];
            int32_t np = node_raw(tokens[1]);
            int32_t nn = node_raw(tokens[2]);

            ParsedSourceSpec spec = parse_source_spec(tokens, 3);
            auto is = std::make_unique<ISource>(name, np, nn, spec.dc_val);
            if (spec.ac_mag != 0.0 || spec.ac_phase != 0.0) {
                is->set_ac(spec.ac_mag, spec.ac_phase);
            }
            if (spec.func == SourceFunction::PULSE) is->set_pulse(spec.pulse);
            else if (spec.func == SourceFunction::SIN) is->set_sin(spec.sin);
            else if (spec.func == SourceFunction::PWL) is->set_pwl(spec.pwl);
            else if (spec.func == SourceFunction::EXP) is->set_exp(spec.exp);
            else if (spec.func == SourceFunction::SFFM) is->set_sffm(spec.sffm);
            else if (spec.func == SourceFunction::AM) is->set_am(spec.am);
            ckt.add_device(std::move(is));

        } else if (elem_type == 'd' || elem_type == 'm' ||
                   elem_type == 'q' || elem_type == 'j' ||
                   elem_type == 'z') {
            // Semiconductor device — dispatch through DeviceRegistry
            auto* handler = DeviceRegistry::get_default().find_parser(elem_type);
            if (handler) {
                auto error_fn = [&line](const std::string& msg) {
                    throw ParseError("Line " + std::to_string(line.line_number) + ": " + msg);
                };
                ParseContext parse_ctx{ckt, node_raw, models, line.line_number, error_fn};
                auto elem = handler->parse(tokens, parse_ctx);
                if (elem) {
                    parsed_elements[elem_type].push_back(std::move(elem));
                }
            }

        } else if (elem_type == 'e') {
            // E name np nn [POLY(N) cp1 cn1 ... coeffs | TABLE {V(in)} = (x,y)... | nc+ nc- gain]
            if (tokens.size() < 4) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": VCVS requires name, np, nn, and source specification");
            }
            std::string name = tokens[0];
            int32_t np  = node_raw(tokens[1]);
            int32_t nn  = node_raw(tokens[2]);

            // Detect POLY or TABLE keyword at token[3]
            std::string tok3 = to_lower(tokens[3]);

            if (tok3.substr(0, 4) == "poly") {
                // POLY(N) form
                // Extract dimension N from "poly(n)" or "poly" followed by "(n)"
                int poly_dim = 1;
                std::string poly_tok = tok3;
                size_t paren_pos = poly_tok.find('(');
                if (paren_pos != std::string::npos) {
                    size_t close = poly_tok.find(')');
                    if (close != std::string::npos && close > paren_pos) {
                        poly_dim = std::stoi(poly_tok.substr(paren_pos + 1, close - paren_pos - 1));
                    }
                } else if (tokens.size() > 4) {
                    // "POLY (N)" — dimension in separate token
                    std::string next = tokens[4];
                    if (!next.empty() && next.front() == '(') {
                        size_t close = next.find(')');
                        if (close != std::string::npos) {
                            poly_dim = std::stoi(next.substr(1, close - 1));
                        }
                    }
                }
                // Now parse 2*poly_dim control node pairs, then coefficients
                size_t idx = 4;
                // Skip past any "(N)" token we haven't consumed yet
                if (idx < tokens.size() && tokens[idx].front() == '(') ++idx;

                std::vector<CtrlPair> ctrl_pairs;
                ctrl_pairs.reserve(poly_dim);
                for (int k = 0; k < poly_dim; ++k) {
                    if (idx + 1 >= tokens.size()) {
                        throw ParseError("Line " + std::to_string(line.line_number) +
                                         ": POLY VCVS: not enough control node pairs");
                    }
                    int32_t cp = node_raw(tokens[idx]);
                    int32_t cn = node_raw(tokens[idx + 1]);
                    ctrl_pairs.push_back({cp, cn});
                    idx += 2;
                }
                // Remaining tokens are polynomial coefficients
                std::vector<double> coeffs;
                for (; idx < tokens.size(); ++idx) {
                    coeffs.push_back(parse_spice_number(tokens[idx]));
                }
                ckt.add_device(std::make_unique<NonlinearVCVS>(
                    name, np, nn, std::move(ctrl_pairs), std::move(coeffs)));

            } else if (tok3 == "table" || tok3.substr(0, 6) == "table{") {
                // TABLE {V(in)} = (x1,y1) (x2,y2) ...
                // Also handle PSpice-style TABLE{V(in)} (no space between TABLE and {)
                // Parse control expression: token[4] should be "{V(node)}" or similar
                std::string table_ctrl_token;
                size_t table_data_start;
                if (tok3.substr(0, 6) == "table{") {
                    // No space: tok3 = "table{v(in)}" — extract from tok3 after "table"
                    table_ctrl_token = tokens[3].substr(5);  // use original case
                    table_data_start = 4;
                } else {
                    if (tokens.size() < 6) {
                        throw ParseError("Line " + std::to_string(line.line_number) +
                                         ": TABLE VCVS requires control expression and table points");
                    }
                    table_ctrl_token = tokens[4];
                    table_data_start = 5;
                }
                // Extract node name from {V(node)}
                std::string ctrl_expr = table_ctrl_token;
                // Strip braces
                if (!ctrl_expr.empty() && ctrl_expr.front() == '{') ctrl_expr.erase(0, 1);
                if (!ctrl_expr.empty() && ctrl_expr.back() == '}') ctrl_expr.pop_back();
                // Extract node from V(node) or v(node)
                std::string ctrl_lower = to_lower(ctrl_expr);
                int32_t ctrl_pos = GROUND_INTERNAL, ctrl_neg = GROUND_INTERNAL;
                if (ctrl_lower.size() > 3 && ctrl_lower[0] == 'v' && ctrl_lower[1] == '(') {
                    size_t close = ctrl_lower.find(')');
                    if (close != std::string::npos) {
                        std::string node_name = ctrl_expr.substr(2, close - 2);
                        // Support differential: V(n1,n2)
                        size_t comma = node_name.find(',');
                        if (comma != std::string::npos) {
                            ctrl_pos = node_raw(node_name.substr(0, comma));
                            ctrl_neg = node_raw(node_name.substr(comma + 1));
                        } else {
                            ctrl_pos = node_raw(node_name);
                            // ctrl_neg stays GROUND_INTERNAL
                        }
                    }
                }
                // Skip "=" token if present
                size_t idx = table_data_start;
                if (idx < tokens.size() && tokens[idx] == "=") ++idx;

                // Parse table points: (x,y) may be in one token or spread across tokens
                // Join remaining tokens and parse (x,y) pairs
                std::string joined;
                for (size_t i = idx; i < tokens.size(); ++i) {
                    joined += tokens[i];
                    joined += ' ';
                }
                std::vector<TablePoint> pts;
                size_t pos = 0;
                while (pos < joined.size()) {
                    // Skip whitespace
                    while (pos < joined.size() && std::isspace(static_cast<unsigned char>(joined[pos]))) ++pos;
                    if (pos >= joined.size()) break;
                    if (joined[pos] != '(') { ++pos; continue; }
                    ++pos;  // skip '('
                    size_t close = joined.find(')', pos);
                    if (close == std::string::npos) break;
                    std::string pair_str = joined.substr(pos, close - pos);
                    pos = close + 1;
                    size_t comma = pair_str.find(',');
                    if (comma == std::string::npos) continue;
                    double px = parse_spice_number(pair_str.substr(0, comma));
                    double py = parse_spice_number(pair_str.substr(comma + 1));
                    pts.push_back({px, py});
                }
                if (pts.empty()) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": TABLE VCVS: no table points found");
                }
                ckt.add_device(std::make_unique<TableVCVS>(
                    name, np, nn, ctrl_pos, ctrl_neg, std::move(pts)));

            } else if (tok3 == "value" || tok3.substr(0, 6) == "value=") {
                // PSpice VALUE={expr} form — lower to ASRCDevice with VOLTAGE mode
                // Extract expression from VALUE={expr} or VALUE = {expr} or VALUE ={expr}
                std::string expr_str;
                if (tok3.substr(0, 6) == "value=") {
                    // VALUE={expr} as single token or VALUE=... — existing path
                    std::string rest;
                    for (size_t i = 3; i < tokens.size(); ++i) {
                        if (!rest.empty()) rest += ' ';
                        rest += tokens[i];
                    }
                    expr_str = rest.substr(rest.find('=') + 1);
                } else {
                    // tok3 == "value", look for = in next tokens
                    size_t expr_start = 4;
                    if (expr_start < tokens.size() && tokens[expr_start][0] == '=') {
                        if (tokens[expr_start].size() > 1) {
                            // "={expr}" — expression starts after =
                            expr_str = tokens[expr_start].substr(1);
                            for (size_t i = expr_start + 1; i < tokens.size(); ++i) {
                                expr_str += ' ';
                                expr_str += tokens[i];
                            }
                        } else {
                            // "=" alone — expression is in remaining tokens
                            for (size_t i = expr_start + 1; i < tokens.size(); ++i) {
                                if (!expr_str.empty()) expr_str += ' ';
                                expr_str += tokens[i];
                            }
                        }
                    } else {
                        throw ParseError("Line " + std::to_string(line.line_number) +
                                         ": E element VALUE form requires VALUE={expr}");
                    }
                }
                // Strip surrounding whitespace
                while (!expr_str.empty() && std::isspace(static_cast<unsigned char>(expr_str.front())))
                    expr_str.erase(0, 1);
                while (!expr_str.empty() && std::isspace(static_cast<unsigned char>(expr_str.back())))
                    expr_str.pop_back();
                // Strip surrounding braces
                if (!expr_str.empty() && expr_str.front() == '{') expr_str.erase(0, 1);
                if (!expr_str.empty() && expr_str.back() == '}') expr_str.pop_back();
                // Strip whitespace again after brace removal
                while (!expr_str.empty() && std::isspace(static_cast<unsigned char>(expr_str.front())))
                    expr_str.erase(0, 1);
                while (!expr_str.empty() && std::isspace(static_cast<unsigned char>(expr_str.back())))
                    expr_str.pop_back();

                if (expr_str.empty()) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": E element VALUE form has empty expression");
                }

                // Expand .func calls
                expr_str = expand_funcs(expr_str, func_defs);

                // Compile the expression
                asrc::CompiledExpression compiled;
                try {
                    compiled = asrc::CompiledExpression::compile(expr_str);
                } catch (const ParseError& e) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": E element VALUE expression error: " + e.what());
                }

                // Resolve variable references (same as B-source)
                const auto& refs = compiled.var_refs();
                int nv = compiled.num_vars();
                std::vector<int32_t> e_node_indices(nv, -1);
                std::vector<int32_t> e_node_indices2(nv, -1);
                std::vector<std::string> e_vsrc_names(nv);

                for (int i = 0; i < nv; ++i) {
                    const auto& ref = refs[i];
                    if (ref.kind == asrc::VarKind::NODE_VOLTAGE &&
                        ref.name1 == "__time__") {
                        e_node_indices[i] = -2;
                        continue;
                    }
                    if (ref.kind == asrc::VarKind::NODE_VOLTAGE &&
                        ref.name1 == "__temper__") {
                        e_node_indices[i] = -2;
                        continue;
                    }
                    if (ref.kind == asrc::VarKind::NODE_VOLTAGE &&
                        ref.name1 == "__hertz__") {
                        e_node_indices[i] = -2;
                        continue;
                    }
                    switch (ref.kind) {
                    case asrc::VarKind::NODE_VOLTAGE: {
                        std::string lname = ref.name1;
                        if (lname == "0" || lname == "gnd") {
                            e_node_indices[i] = GROUND_INTERNAL;
                        } else {
                            e_node_indices[i] = node_raw(lname);
                        }
                        break;
                    }
                    case asrc::VarKind::DIFF_VOLTAGE: {
                        std::string ln1 = ref.name1;
                        std::string ln2 = ref.name2;
                        e_node_indices[i]  = (ln1 == "0" || ln1 == "gnd")
                                             ? GROUND_INTERNAL : node_raw(ln1);
                        e_node_indices2[i] = (ln2 == "0" || ln2 == "gnd")
                                             ? GROUND_INTERNAL : node_raw(ln2);
                        break;
                    }
                    case asrc::VarKind::BRANCH_CURRENT:
                        e_vsrc_names[i] = ref.name1;
                        break;
                    }
                }

                DeferredASRC bd;
                bd.name = name;
                bd.np = np;
                bd.nn = nn;
                bd.mode = ASRCDevice::Mode::VOLTAGE;
                bd.expr = std::move(compiled);
                bd.node_indices = std::move(e_node_indices);
                bd.node_indices2 = std::move(e_node_indices2);
                bd.vsrc_names = std::move(e_vsrc_names);
                bd.line_number = line.line_number;
                bd.tc1 = 0.0;
                bd.tc2 = 0.0;
                bd.temp = -1.0;
                bd.dtemp = 0.0;
                deferred_asrcs.push_back(std::move(bd));

            } else {
                // Linear form: E name np nn nc+ nc- gain
                if (tokens.size() < 6) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": VCVS requires name, np, nn, nc+, nc-, gain");
                }
                int32_t ncp = node_raw(tokens[3]);
                int32_t ncn = node_raw(tokens[4]);
                double  gain = parse_spice_number(tokens[5]);
                ckt.add_device(std::make_unique<VCVS>(name, np, nn, ncp, ncn, gain));
            }

        } else if (elem_type == 'g') {
            // G name np nn [POLY(N) cp1 cn1 ... coeffs | TABLE {V(in)} = (x,y)... | nc+ nc- gm]
            if (tokens.size() < 4) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": VCCS requires name, np, nn, and source specification");
            }
            std::string name = tokens[0];
            int32_t np  = node_raw(tokens[1]);
            int32_t nn  = node_raw(tokens[2]);

            std::string tok3g = to_lower(tokens[3]);

            if (tok3g.substr(0, 4) == "poly") {
                // POLY(N) form for VCCS
                int poly_dim = 1;
                std::string poly_tok = tok3g;
                size_t paren_pos = poly_tok.find('(');
                if (paren_pos != std::string::npos) {
                    size_t close = poly_tok.find(')');
                    if (close != std::string::npos && close > paren_pos) {
                        poly_dim = std::stoi(poly_tok.substr(paren_pos + 1, close - paren_pos - 1));
                    }
                } else if (tokens.size() > 4) {
                    std::string next = tokens[4];
                    if (!next.empty() && next.front() == '(') {
                        size_t close = next.find(')');
                        if (close != std::string::npos) {
                            poly_dim = std::stoi(next.substr(1, close - 1));
                        }
                    }
                }
                size_t idx = 4;
                if (idx < tokens.size() && tokens[idx].front() == '(') ++idx;

                std::vector<CtrlPair> ctrl_pairs;
                ctrl_pairs.reserve(poly_dim);
                for (int k = 0; k < poly_dim; ++k) {
                    if (idx + 1 >= tokens.size()) {
                        throw ParseError("Line " + std::to_string(line.line_number) +
                                         ": POLY VCCS: not enough control node pairs");
                    }
                    int32_t cp = node_raw(tokens[idx]);
                    int32_t cn = node_raw(tokens[idx + 1]);
                    ctrl_pairs.push_back({cp, cn});
                    idx += 2;
                }
                std::vector<double> coeffs;
                for (; idx < tokens.size(); ++idx) {
                    coeffs.push_back(parse_spice_number(tokens[idx]));
                }
                ckt.add_device(std::make_unique<NonlinearVCCS>(
                    name, np, nn, std::move(ctrl_pairs), std::move(coeffs)));

            } else if (tok3g == "table" || tok3g.substr(0, 6) == "table{") {
                // TABLE {V(node)} = (x,y) ...
                // Also handle PSpice-style TABLE{V(in)} (no space between TABLE and {)
                std::string table_ctrl_token;
                size_t table_data_start;
                if (tok3g.substr(0, 6) == "table{") {
                    table_ctrl_token = tokens[3].substr(5);
                    table_data_start = 4;
                } else {
                    if (tokens.size() < 6) {
                        throw ParseError("Line " + std::to_string(line.line_number) +
                                         ": TABLE VCCS requires control expression and table points");
                    }
                    table_ctrl_token = tokens[4];
                    table_data_start = 5;
                }
                std::string ctrl_expr = table_ctrl_token;
                if (!ctrl_expr.empty() && ctrl_expr.front() == '{') ctrl_expr.erase(0, 1);
                if (!ctrl_expr.empty() && ctrl_expr.back() == '}') ctrl_expr.pop_back();
                std::string ctrl_lower = to_lower(ctrl_expr);
                int32_t ctrl_pos = GROUND_INTERNAL, ctrl_neg = GROUND_INTERNAL;
                if (ctrl_lower.size() > 3 && ctrl_lower[0] == 'v' && ctrl_lower[1] == '(') {
                    size_t close = ctrl_lower.find(')');
                    if (close != std::string::npos) {
                        std::string node_name = ctrl_expr.substr(2, close - 2);
                        size_t comma = node_name.find(',');
                        if (comma != std::string::npos) {
                            ctrl_pos = node_raw(node_name.substr(0, comma));
                            ctrl_neg = node_raw(node_name.substr(comma + 1));
                        } else {
                            ctrl_pos = node_raw(node_name);
                        }
                    }
                }
                size_t idx = table_data_start;
                if (idx < tokens.size() && tokens[idx] == "=") ++idx;

                std::string joined;
                for (size_t i = idx; i < tokens.size(); ++i) {
                    joined += tokens[i];
                    joined += ' ';
                }
                std::vector<TablePoint> pts;
                size_t pos = 0;
                while (pos < joined.size()) {
                    while (pos < joined.size() && std::isspace(static_cast<unsigned char>(joined[pos]))) ++pos;
                    if (pos >= joined.size()) break;
                    if (joined[pos] != '(') { ++pos; continue; }
                    ++pos;
                    size_t close = joined.find(')', pos);
                    if (close == std::string::npos) break;
                    std::string pair_str = joined.substr(pos, close - pos);
                    pos = close + 1;
                    size_t comma = pair_str.find(',');
                    if (comma == std::string::npos) continue;
                    double px = parse_spice_number(pair_str.substr(0, comma));
                    double py = parse_spice_number(pair_str.substr(comma + 1));
                    pts.push_back({px, py});
                }
                if (pts.empty()) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": TABLE VCCS: no table points found");
                }
                ckt.add_device(std::make_unique<TableVCCS>(
                    name, np, nn, ctrl_pos, ctrl_neg, std::move(pts)));

            } else if (tok3g == "value" || tok3g.substr(0, 6) == "value=") {
                // PSpice VALUE={expr} form — lower to ASRCDevice with CURRENT mode
                // Extract expression from VALUE={expr} or VALUE = {expr} or VALUE ={expr}
                std::string expr_str;
                if (tok3g.substr(0, 6) == "value=") {
                    // VALUE={expr} as single token or VALUE=... — existing path
                    std::string rest;
                    for (size_t i = 3; i < tokens.size(); ++i) {
                        if (!rest.empty()) rest += ' ';
                        rest += tokens[i];
                    }
                    expr_str = rest.substr(rest.find('=') + 1);
                } else {
                    // tok3g == "value", look for = in next tokens
                    size_t expr_start = 4;
                    if (expr_start < tokens.size() && tokens[expr_start][0] == '=') {
                        if (tokens[expr_start].size() > 1) {
                            // "={expr}" — expression starts after =
                            expr_str = tokens[expr_start].substr(1);
                            for (size_t i = expr_start + 1; i < tokens.size(); ++i) {
                                expr_str += ' ';
                                expr_str += tokens[i];
                            }
                        } else {
                            // "=" alone — expression is in remaining tokens
                            for (size_t i = expr_start + 1; i < tokens.size(); ++i) {
                                if (!expr_str.empty()) expr_str += ' ';
                                expr_str += tokens[i];
                            }
                        }
                    } else {
                        throw ParseError("Line " + std::to_string(line.line_number) +
                                         ": G element VALUE form requires VALUE={expr}");
                    }
                }
                while (!expr_str.empty() && std::isspace(static_cast<unsigned char>(expr_str.front())))
                    expr_str.erase(0, 1);
                while (!expr_str.empty() && std::isspace(static_cast<unsigned char>(expr_str.back())))
                    expr_str.pop_back();
                if (!expr_str.empty() && expr_str.front() == '{') expr_str.erase(0, 1);
                if (!expr_str.empty() && expr_str.back() == '}') expr_str.pop_back();
                while (!expr_str.empty() && std::isspace(static_cast<unsigned char>(expr_str.front())))
                    expr_str.erase(0, 1);
                while (!expr_str.empty() && std::isspace(static_cast<unsigned char>(expr_str.back())))
                    expr_str.pop_back();

                if (expr_str.empty()) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": G element VALUE form has empty expression");
                }

                expr_str = expand_funcs(expr_str, func_defs);

                asrc::CompiledExpression compiled;
                try {
                    compiled = asrc::CompiledExpression::compile(expr_str);
                } catch (const ParseError& e) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": G element VALUE expression error: " + e.what());
                }

                const auto& refs = compiled.var_refs();
                int nv = compiled.num_vars();
                std::vector<int32_t> g_node_indices(nv, -1);
                std::vector<int32_t> g_node_indices2(nv, -1);
                std::vector<std::string> g_vsrc_names(nv);

                for (int i = 0; i < nv; ++i) {
                    const auto& ref = refs[i];
                    if (ref.kind == asrc::VarKind::NODE_VOLTAGE &&
                        ref.name1 == "__time__") {
                        g_node_indices[i] = -2;
                        continue;
                    }
                    if (ref.kind == asrc::VarKind::NODE_VOLTAGE &&
                        ref.name1 == "__temper__") {
                        g_node_indices[i] = -2;
                        continue;
                    }
                    if (ref.kind == asrc::VarKind::NODE_VOLTAGE &&
                        ref.name1 == "__hertz__") {
                        g_node_indices[i] = -2;
                        continue;
                    }
                    switch (ref.kind) {
                    case asrc::VarKind::NODE_VOLTAGE: {
                        std::string lname = ref.name1;
                        if (lname == "0" || lname == "gnd") {
                            g_node_indices[i] = GROUND_INTERNAL;
                        } else {
                            g_node_indices[i] = node_raw(lname);
                        }
                        break;
                    }
                    case asrc::VarKind::DIFF_VOLTAGE: {
                        std::string ln1 = ref.name1;
                        std::string ln2 = ref.name2;
                        g_node_indices[i]  = (ln1 == "0" || ln1 == "gnd")
                                             ? GROUND_INTERNAL : node_raw(ln1);
                        g_node_indices2[i] = (ln2 == "0" || ln2 == "gnd")
                                             ? GROUND_INTERNAL : node_raw(ln2);
                        break;
                    }
                    case asrc::VarKind::BRANCH_CURRENT:
                        g_vsrc_names[i] = ref.name1;
                        break;
                    }
                }

                DeferredASRC bd;
                bd.name = name;
                bd.np = np;
                bd.nn = nn;
                bd.mode = ASRCDevice::Mode::CURRENT;
                bd.expr = std::move(compiled);
                bd.node_indices = std::move(g_node_indices);
                bd.node_indices2 = std::move(g_node_indices2);
                bd.vsrc_names = std::move(g_vsrc_names);
                bd.line_number = line.line_number;
                bd.tc1 = 0.0;
                bd.tc2 = 0.0;
                bd.temp = -1.0;
                bd.dtemp = 0.0;
                deferred_asrcs.push_back(std::move(bd));

            } else {
                // Linear form: G name np nn nc+ nc- gm
                if (tokens.size() < 6) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": VCCS requires name, np, nn, nc+, nc-, gm");
                }
                int32_t ncp = node_raw(tokens[3]);
                int32_t ncn = node_raw(tokens[4]);
                double  gm  = parse_spice_number(tokens[5]);
                auto vccs = std::make_unique<VCCS>(name, np, nn, ncp, ncn, gm);
                for (size_t k = 6; k < tokens.size(); ++k) {
                    std::string tok = to_lower(tokens[k]);
                    if (tok.starts_with("m="))
                        vccs->set_multiplier(parse_spice_number(tok.substr(2)));
                }
                ckt.add_device(std::move(vccs));
            }

        } else if (elem_type == 'h') {
            // H name np nn [POLY(N) Vs1 ... coeffs | Vsense transresistance]
            if (tokens.size() < 5) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": CCVS requires name, np, nn, Vsense, transresistance");
            }
            std::string tok3h = to_lower(tokens[3]);
            if (tok3h.substr(0, 4) == "poly") {
                // POLY(N) form
                int poly_dim = 1;
                std::string poly_tok = tok3h;
                size_t paren_pos = poly_tok.find('(');
                if (paren_pos != std::string::npos) {
                    size_t close = poly_tok.find(')');
                    if (close != std::string::npos && close > paren_pos) {
                        poly_dim = std::stoi(poly_tok.substr(paren_pos + 1, close - paren_pos - 1));
                    }
                } else if (tokens.size() > 4) {
                    std::string next = tokens[4];
                    if (!next.empty() && next.front() == '(') {
                        size_t close = next.find(')');
                        if (close != std::string::npos) {
                            poly_dim = std::stoi(next.substr(1, close - 1));
                        }
                    }
                }
                size_t idx = 4;
                if (idx < tokens.size() && tokens[idx].front() == '(') ++idx;

                // Parse N VSource names
                std::vector<std::string> vsense_names;
                vsense_names.reserve(poly_dim);
                for (int k = 0; k < poly_dim; ++k) {
                    if (idx >= tokens.size()) {
                        throw ParseError("Line " + std::to_string(line.line_number) +
                                         ": POLY CCVS: not enough sensing VSource names");
                    }
                    vsense_names.push_back(tokens[idx++]);
                }
                // Remaining tokens are polynomial coefficients
                std::vector<double> coeffs;
                for (; idx < tokens.size(); ++idx) {
                    coeffs.push_back(parse_spice_number(tokens[idx]));
                }
                DeferredPolyCCVS hpd;
                hpd.name = tokens[0];
                hpd.np = node_raw(tokens[1]);
                hpd.nn = node_raw(tokens[2]);
                hpd.vsense_names = std::move(vsense_names);
                hpd.coeffs = std::move(coeffs);
                hpd.line_number = line.line_number;
                deferred_poly_ccvs.push_back(std::move(hpd));
            } else {
                // Linear form: H name np nn Vsense transresistance
                DeferredCCVS hd;
                hd.name        = tokens[0];
                hd.np          = node_raw(tokens[1]);
                hd.nn          = node_raw(tokens[2]);
                hd.vsense_name = tokens[3];
                hd.rm          = parse_spice_number(tokens[4]);
                hd.line_number = line.line_number;
                deferred_ccvs.push_back(std::move(hd));
            }

        } else if (elem_type == 'f') {
            // F name np nn [POLY(N) Vs1 ... coeffs | Vsense gain]
            if (tokens.size() < 5) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": CCCS requires name, np, nn, Vsense, gain");
            }
            std::string tok3f = to_lower(tokens[3]);
            if (tok3f.substr(0, 4) == "poly") {
                // POLY(N) form
                int poly_dim = 1;
                std::string poly_tok = tok3f;
                size_t paren_pos = poly_tok.find('(');
                if (paren_pos != std::string::npos) {
                    size_t close = poly_tok.find(')');
                    if (close != std::string::npos && close > paren_pos) {
                        poly_dim = std::stoi(poly_tok.substr(paren_pos + 1, close - paren_pos - 1));
                    }
                } else if (tokens.size() > 4) {
                    std::string next = tokens[4];
                    if (!next.empty() && next.front() == '(') {
                        size_t close = next.find(')');
                        if (close != std::string::npos) {
                            poly_dim = std::stoi(next.substr(1, close - 1));
                        }
                    }
                }
                size_t idx = 4;
                if (idx < tokens.size() && tokens[idx].front() == '(') ++idx;

                // Parse N VSource names
                std::vector<std::string> vsense_names;
                vsense_names.reserve(poly_dim);
                for (int k = 0; k < poly_dim; ++k) {
                    if (idx >= tokens.size()) {
                        throw ParseError("Line " + std::to_string(line.line_number) +
                                         ": POLY CCCS: not enough sensing VSource names");
                    }
                    vsense_names.push_back(tokens[idx++]);
                }
                // Remaining tokens are polynomial coefficients
                std::vector<double> coeffs;
                for (; idx < tokens.size(); ++idx) {
                    coeffs.push_back(parse_spice_number(tokens[idx]));
                }
                DeferredPolyCCCS fpd;
                fpd.name = tokens[0];
                fpd.np = node_raw(tokens[1]);
                fpd.nn = node_raw(tokens[2]);
                fpd.vsense_names = std::move(vsense_names);
                fpd.coeffs = std::move(coeffs);
                fpd.line_number = line.line_number;
                deferred_poly_cccs.push_back(std::move(fpd));
            } else {
                // Linear form: F name np nn Vsense gain
                DeferredCCCS fd;
                fd.name        = tokens[0];
                fd.np          = node_raw(tokens[1]);
                fd.nn          = node_raw(tokens[2]);
                fd.vsense_name = tokens[3];
                fd.gain        = parse_spice_number(tokens[4]);
                fd.line_number = line.line_number;
                for (size_t k = 5; k < tokens.size(); ++k) {
                    std::string tok = to_lower(tokens[k]);
                    if (tok.starts_with("m="))
                        fd.m = parse_spice_number(tok.substr(2));
                }
                deferred_cccs.push_back(std::move(fd));
            }

        } else if (elem_type == 'k') {
            // K name L1 L2 coupling_coefficient
            if (tokens.size() < 4) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": K element requires name, L1, L2, coupling_coefficient");
            }
            DeferredCoupledInductor kd;
            kd.name = tokens[0];
            kd.l1_name = tokens[1];
            kd.l2_name = tokens[2];
            kd.coupling = parse_spice_number(tokens[3]);
            kd.line_number = line.line_number;
            if (to_lower(kd.l1_name) == to_lower(kd.l2_name)) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": K element cannot couple an inductor to itself");
            }
            deferred_coupled_inductors.push_back(std::move(kd));

        } else if (elem_type == 't') {
            // T name p1+ p1- p2+ p2- Z0=val TD=val
            if (tokens.size() < 6) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": T element requires name, p1+, p1-, p2+, p2-, Z0=val TD=val");
            }
            std::string tname = tokens[0];
            int32_t tp1p = node_raw(tokens[1]);
            int32_t tp1n = node_raw(tokens[2]);
            int32_t tp2p = node_raw(tokens[3]);
            int32_t tp2n = node_raw(tokens[4]);

            double tz0 = 0.0, ttd = -1.0, tf = -1.0, tnl = -1.0;
            double ic_v1 = 0, ic_i1 = 0, ic_v2 = 0, ic_i2 = 0;
            bool z0_given = false, ic_given = false;
            for (size_t i = 5; i < tokens.size(); ++i) {
                auto eq = tokens[i].find('=');
                if (eq == std::string::npos) continue;
                std::string key = to_lower(tokens[i].substr(0, eq));
                if (key == "ic") {
                    std::string ic_str = tokens[i].substr(eq + 1);
                    std::vector<double> ic_vals;
                    std::stringstream ss(ic_str);
                    std::string tok;
                    while (std::getline(ss, tok, ',')) {
                        ic_vals.push_back(parse_spice_number(tok));
                    }
                    if (ic_vals.size() >= 1) ic_v1 = ic_vals[0];
                    if (ic_vals.size() >= 2) ic_i1 = ic_vals[1];
                    if (ic_vals.size() >= 3) ic_v2 = ic_vals[2];
                    if (ic_vals.size() >= 4) ic_i2 = ic_vals[3];
                    ic_given = true;
                } else {
                    double val = parse_spice_number(tokens[i].substr(eq + 1));
                    if      (key == "z0") { tz0 = val; z0_given = true; }
                    else if (key == "td") { ttd = val; }
                    else if (key == "f")  { tf  = val; }
                    else if (key == "nl") { tnl = val; }
                }
            }
            if (!z0_given) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": T element '" + tname + "' missing Z0=");
            }
            if (tz0 <= 0.0) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": T element '" + tname + "' Z0 must be positive");
            }
            if (ttd < 0.0) {
                if (tf > 0.0 && tnl > 0.0) {
                    ttd = tnl / tf;
                } else if (tf > 0.0 && tnl < 0.0) {
                    ttd = 0.25 / tf;
                } else {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": T element '" + tname + "' requires TD= or F= (with optional NL=)");
                }
            }
            auto tl = std::make_unique<TransmissionLine>(tname, tp1p, tp1n, tp2p, tp2n, tz0, ttd);
            if (ic_given)
                tl->set_ic(ic_v1, ic_i1, ic_v2, ic_i2);
            ckt.add_device(std::move(tl));

        } else if (elem_type == 'o') {
            // O name p1+ p1- p2+ p2- modelname [IC=v1,i1,v2,i2]
            if (tokens.size() < 6) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": O element requires name, p1+, p1-, p2+, p2-, modelname");
            }
            DeferredLTRA ol;
            ol.name       = tokens[0];
            ol.p1p        = node_raw(tokens[1]);
            ol.p1n        = node_raw(tokens[2]);
            ol.p2p        = node_raw(tokens[3]);
            ol.p2n        = node_raw(tokens[4]);
            ol.model_name = tokens[5];
            ol.line_number = line.line_number;

            // Parse optional parameters after modelname (IC=v1,i1,v2,i2 or V1= I1= V2= I2=)
            for (size_t ti = 6; ti < tokens.size(); ++ti) {
                std::string tok_lower = to_lower(tokens[ti]);
                if (tok_lower.substr(0, 3) == "ic=") {
                    // IC=v1[,i1[,v2[,i2]]]
                    std::string ic_str = tokens[ti].substr(3);
                    std::vector<double> ic_vals;
                    std::istringstream iss(ic_str);
                    std::string val_str;
                    while (std::getline(iss, val_str, ',')) {
                        if (!val_str.empty()) {
                            try { ic_vals.push_back(std::stod(val_str)); }
                            catch (...) { break; }
                        }
                    }
                    if (ic_vals.size() >= 1) { ol.ic_v1 = ic_vals[0]; ol.ic_v1_given = true; }
                    if (ic_vals.size() >= 2) { ol.ic_i1 = ic_vals[1]; ol.ic_i1_given = true; }
                    if (ic_vals.size() >= 3) { ol.ic_v2 = ic_vals[2]; ol.ic_v2_given = true; }
                    if (ic_vals.size() >= 4) { ol.ic_i2 = ic_vals[3]; ol.ic_i2_given = true; }
                } else if (tok_lower.substr(0, 3) == "v1=") {
                    try { ol.ic_v1 = std::stod(tokens[ti].substr(3)); ol.ic_v1_given = true; }
                    catch (...) {}
                } else if (tok_lower.substr(0, 3) == "i1=") {
                    try { ol.ic_i1 = std::stod(tokens[ti].substr(3)); ol.ic_i1_given = true; }
                    catch (...) {}
                } else if (tok_lower.substr(0, 3) == "v2=") {
                    try { ol.ic_v2 = std::stod(tokens[ti].substr(3)); ol.ic_v2_given = true; }
                    catch (...) {}
                } else if (tok_lower.substr(0, 3) == "i2=") {
                    try { ol.ic_i2 = std::stod(tokens[ti].substr(3)); ol.ic_i2_given = true; }
                    catch (...) {}
                }
            }
            deferred_ltras.push_back(std::move(ol));

        } else if (elem_type == 's') {
            // S name n+ n- nc+ nc- modelname
            if (tokens.size() < 6) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": S element requires name, n+, n-, nc+, nc-, modelname");
            }
            DeferredVSwitch sd;
            sd.name       = tokens[0];
            sd.np         = node_raw(tokens[1]);
            sd.nn         = node_raw(tokens[2]);
            sd.ncp        = node_raw(tokens[3]);
            sd.ncn        = node_raw(tokens[4]);
            sd.model_name = tokens[5];
            sd.line_number = line.line_number;
            deferred_vswitches.push_back(std::move(sd));

        } else if (elem_type == 'w') {
            // W name n+ n- Vsense modelname
            if (tokens.size() < 5) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": W element requires name, n+, n-, Vsense, modelname");
            }
            DeferredCSwitch wd;
            wd.name        = tokens[0];
            wd.np          = node_raw(tokens[1]);
            wd.nn          = node_raw(tokens[2]);
            wd.vsense_name = tokens[3];
            wd.model_name  = tokens[4];
            wd.line_number = line.line_number;
            deferred_cswitches.push_back(std::move(wd));

        } else if (elem_type == 'b') {
            // B name np nn V={expression} or I={expression} [tc1=val] [tc2=val] [temp=val] [dtemp=val]
            // Syntax: Bname node+ node- V={expr} | I={expr}
            if (tokens.size() < 4) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": B element requires name, np, nn, V={expr} or I={expr}");
            }
            std::string name = tokens[0];
            int32_t np = node_raw(tokens[1]);
            int32_t nn = node_raw(tokens[2]);

            // Separate expression tokens from instance parameter tokens (tc1=, tc2=, temp=, dtemp=)
            // Parameters are recognized as trailing tokens with known prefixes.
            double b_tc1 = 0.0, b_tc2 = 0.0, b_temp = -1.0, b_dtemp = 0.0;
            std::vector<std::string> expr_tokens;
            for (size_t i = 3; i < tokens.size(); ++i) {
                std::string tok_lower = to_lower(tokens[i]);
                if (tok_lower.starts_with("tc1=")) {
                    b_tc1 = parse_spice_number(tok_lower.substr(4));
                } else if (tok_lower.starts_with("tc2=")) {
                    b_tc2 = parse_spice_number(tok_lower.substr(4));
                } else if (tok_lower.starts_with("temp=")) {
                    b_temp = parse_spice_number(tok_lower.substr(5)) + 273.15;
                } else if (tok_lower.starts_with("dtemp=")) {
                    b_dtemp = parse_spice_number(tok_lower.substr(6));
                } else {
                    expr_tokens.push_back(tokens[i]);
                }
            }

            // Join expression tokens to handle expressions split across tokens
            std::string rest;
            for (size_t i = 0; i < expr_tokens.size(); ++i) {
                if (!rest.empty()) rest += ' ';
                rest += expr_tokens[i];
            }

            // Determine mode (V= or I=) and extract expression
            ASRCDevice::Mode mode;
            std::string expr_str;
            std::string rest_lower = to_lower(rest);

            if (rest_lower.size() >= 2 && rest_lower[0] == 'v' && rest_lower[1] == '=') {
                mode = ASRCDevice::Mode::VOLTAGE;
                expr_str = rest.substr(2);
            } else if (rest_lower.size() >= 2 && rest_lower[0] == 'i' && rest_lower[1] == '=') {
                mode = ASRCDevice::Mode::CURRENT;
                expr_str = rest.substr(2);
            } else {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": B element requires V={expr} or I={expr}, got '" + rest + "'");
            }

            // Strip optional surrounding braces from expression
            while (!expr_str.empty() && std::isspace(static_cast<unsigned char>(expr_str.front())))
                expr_str.erase(0, 1);
            while (!expr_str.empty() && std::isspace(static_cast<unsigned char>(expr_str.back())))
                expr_str.pop_back();

            // Expand .func calls in the joined expression string
            // (token-level expansion may miss multi-token func calls like myfunc(V(in1), V(in2)))
            expr_str = expand_funcs(expr_str, func_defs);

            // Compile the expression
            asrc::CompiledExpression compiled;
            try {
                compiled = asrc::CompiledExpression::compile(expr_str);
            } catch (const ParseError& e) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": B element expression error: " + e.what());
            }

            // Resolve variable references: V() refs use node_raw(), I() refs deferred
            const auto& refs = compiled.var_refs();
            int nv = compiled.num_vars();
            std::vector<int32_t> node_indices(nv, -1);
            std::vector<int32_t> node_indices2(nv, -1);
            std::vector<std::string> vsrc_names(nv);

            for (int i = 0; i < nv; ++i) {
                const auto& ref = refs[i];
                if (ref.kind == asrc::VarKind::NODE_VOLTAGE &&
                    ref.name1 == "__time__") {
                    node_indices[i] = -2;  // TIME sentinel
                    continue;
                }
                if (ref.kind == asrc::VarKind::NODE_VOLTAGE &&
                    ref.name1 == "__temper__") {
                    node_indices[i] = -2;  // TEMPER sentinel
                    continue;
                }
                if (ref.kind == asrc::VarKind::NODE_VOLTAGE &&
                    ref.name1 == "__hertz__") {
                    node_indices[i] = -2;  // HERTZ sentinel
                    continue;
                }

                switch (ref.kind) {
                case asrc::VarKind::NODE_VOLTAGE: {
                    std::string lname = ref.name1;
                    if (lname == "0" || lname == "gnd") {
                        node_indices[i] = GROUND_INTERNAL;
                    } else {
                        node_indices[i] = node_raw(lname);
                    }
                    break;
                }
                case asrc::VarKind::DIFF_VOLTAGE: {
                    std::string ln1 = ref.name1;
                    std::string ln2 = ref.name2;
                    node_indices[i]  = (ln1 == "0" || ln1 == "gnd")
                                       ? GROUND_INTERNAL : node_raw(ln1);
                    node_indices2[i] = (ln2 == "0" || ln2 == "gnd")
                                       ? GROUND_INTERNAL : node_raw(ln2);
                    break;
                }
                case asrc::VarKind::BRANCH_CURRENT:
                    vsrc_names[i] = ref.name1;  // resolved in deferred pass
                    break;
                }
            }

            DeferredASRC bd;
            bd.name = name;
            bd.np = np;
            bd.nn = nn;
            bd.mode = mode;
            bd.expr = std::move(compiled);
            bd.node_indices = std::move(node_indices);
            bd.node_indices2 = std::move(node_indices2);
            bd.vsrc_names = std::move(vsrc_names);
            bd.line_number = line.line_number;
            bd.tc1 = b_tc1;
            bd.tc2 = b_tc2;
            bd.temp = b_temp;
            bd.dtemp = b_dtemp;
            deferred_asrcs.push_back(std::move(bd));

        }
        // Ignore unknown lines
    }
}

// ---------------------------------------------------------------------------
// Pass 3: Resolve deferred elements
// ---------------------------------------------------------------------------
void NetlistParser::pass3_resolve_deferred(ParseState& state) {
    auto& ckt = state.ckt;
    auto& node_raw = state.node_raw;
    auto& models = state.models;
    auto& deferred_ccvs = state.deferred_ccvs;
    auto& deferred_cccs = state.deferred_cccs;
    auto& deferred_poly_ccvs = state.deferred_poly_ccvs;
    auto& deferred_poly_cccs = state.deferred_poly_cccs;
    auto& deferred_coupled_inductors = state.deferred_coupled_inductors;
    auto& deferred_vswitches = state.deferred_vswitches;
    auto& deferred_cswitches = state.deferred_cswitches;
    auto& deferred_ltras = state.deferred_ltras;
    auto& deferred_asrcs = state.deferred_asrcs;
    auto& parsed_elements = state.parsed_elements;

    // Resolve deferred CCVS (H elements) — find sensing VSource by name.
    // VSource devices were already added to ckt during Pass 2.
    for (const auto& hd : deferred_ccvs) {
        const VSource* vs = nullptr;
        for (const auto& dev : ckt.devices()) {
            if (auto* v = dynamic_cast<const VSource*>(dev.get())) {
                if (to_lower(v->name()) == to_lower(hd.vsense_name)) {
                    vs = v;
                    break;
                }
            }
        }
        if (!vs) {
            throw ParseError("Line " + std::to_string(hd.line_number) +
                             ": CCVS '" + hd.name + "' references unknown voltage source '" +
                             hd.vsense_name + "'");
        }
        ckt.add_device(std::make_unique<CCVS>(hd.name, hd.np, hd.nn, hd.rm, vs));
    }

    // Resolve deferred CCCS (F elements) — find sensing VSource by name.
    for (const auto& fd : deferred_cccs) {
        const VSource* vs = nullptr;
        for (const auto& dev : ckt.devices()) {
            if (auto* v = dynamic_cast<const VSource*>(dev.get())) {
                if (to_lower(v->name()) == to_lower(fd.vsense_name)) {
                    vs = v;
                    break;
                }
            }
        }
        if (!vs) {
            throw ParseError("Line " + std::to_string(fd.line_number) +
                             ": CCCS '" + fd.name + "' references unknown voltage source '" +
                             fd.vsense_name + "'");
        }
        auto cccs = std::make_unique<CCCS>(fd.name, fd.np, fd.nn, fd.gain, vs);
        if (fd.m != 1.0) cccs->set_multiplier(fd.m);
        ckt.add_device(std::move(cccs));
    }

    // Resolve deferred POLY CCVS (H POLY elements) — find sensing VSources by name.
    for (const auto& hpd : deferred_poly_ccvs) {
        std::vector<const VSource*> vsenses;
        vsenses.reserve(hpd.vsense_names.size());
        for (const auto& vsname : hpd.vsense_names) {
            const VSource* vs = nullptr;
            for (const auto& dev : ckt.devices()) {
                if (auto* v = dynamic_cast<const VSource*>(dev.get())) {
                    if (to_lower(v->name()) == to_lower(vsname)) {
                        vs = v;
                        break;
                    }
                }
            }
            if (!vs) {
                throw ParseError("Line " + std::to_string(hpd.line_number) +
                                 ": POLY CCVS '" + hpd.name +
                                 "' references unknown voltage source '" + vsname + "'");
            }
            vsenses.push_back(vs);
        }
        ckt.add_device(std::make_unique<NonlinearCCVS>(
            hpd.name, hpd.np, hpd.nn, std::move(vsenses), hpd.coeffs));
    }

    // Resolve deferred POLY CCCS (F POLY elements) — find sensing VSources by name.
    for (const auto& fpd : deferred_poly_cccs) {
        std::vector<const VSource*> vsenses;
        vsenses.reserve(fpd.vsense_names.size());
        for (const auto& vsname : fpd.vsense_names) {
            const VSource* vs = nullptr;
            for (const auto& dev : ckt.devices()) {
                if (auto* v = dynamic_cast<const VSource*>(dev.get())) {
                    if (to_lower(v->name()) == to_lower(vsname)) {
                        vs = v;
                        break;
                    }
                }
            }
            if (!vs) {
                throw ParseError("Line " + std::to_string(fpd.line_number) +
                                 ": POLY CCCS '" + fpd.name +
                                 "' references unknown voltage source '" + vsname + "'");
            }
            vsenses.push_back(vs);
        }
        ckt.add_device(std::make_unique<NonlinearCCCS>(
            fpd.name, fpd.np, fpd.nn, std::move(vsenses), fpd.coeffs));
    }

    // Resolve semiconductor devices via DeviceRegistry element parsers
    {
        auto error_fn = [](const std::string& msg) {
            throw ParseError(msg);
        };
        auto& reg = DeviceRegistry::get_default();
        for (auto& [prefix, handler_entry] : reg.element_parsers()) {
            auto pe_it = parsed_elements.find(prefix);
            if (pe_it != parsed_elements.end() && !pe_it->second.empty()) {
                ParseContext resolve_ctx{ckt, node_raw, models, 0, error_fn};
                handler_entry.resolve(pe_it->second, models, ckt, resolve_ctx);
            }
        }
    }


    // Resolve deferred coupled inductors (K elements) — find inductor devices by name.
    for (const auto& kd : deferred_coupled_inductors) {
        Inductor* l1 = nullptr;
        Inductor* l2 = nullptr;
        for (auto& dev : ckt.devices()) {
            if (auto* ind = dynamic_cast<Inductor*>(dev.get())) {
                if (to_lower(ind->name()) == to_lower(kd.l1_name)) {
                    l1 = ind;
                } else if (to_lower(ind->name()) == to_lower(kd.l2_name)) {
                    l2 = ind;
                }
            }
        }
        if (!l1) {
            throw ParseError("Line " + std::to_string(kd.line_number) +
                             ": K element '" + kd.name + "' references unknown inductor '" +
                             kd.l1_name + "'");
        }
        if (!l2) {
            throw ParseError("Line " + std::to_string(kd.line_number) +
                             ": K element '" + kd.name + "' references unknown inductor '" +
                             kd.l2_name + "'");
        }
        ckt.add_device(std::make_unique<CoupledInductor>(kd.name, l1, l2, kd.coupling));
    }

    // Resolve deferred VSwitch (S elements)
    for (const auto& sd : deferred_vswitches) {
        auto it = models.find(sd.model_name);
        if (it == models.end()) {
            auto it2 = models.find(to_lower(sd.model_name));
            if (it2 == models.end()) {
                throw ParseError("Line " + std::to_string(sd.line_number) +
                                 ": Unknown model '" + sd.model_name + "'");
            }
            it = it2;
        }
        std::string model_type = to_lower(it->second.type);
        if (model_type != "sw") {
            throw ParseError("Line " + std::to_string(sd.line_number) +
                             ": S element references non-SW model '" + sd.model_name + "'");
        }
        SwitchModel sm = to_switch_model(it->second);
        ckt.add_device(std::make_unique<VSwitch>(
            sd.name, sd.np, sd.nn, sd.ncp, sd.ncn, sm));
    }

    // Resolve deferred CSwitch (W elements)
    for (const auto& wd : deferred_cswitches) {
        // Look up the model card
        auto it = models.find(wd.model_name);
        if (it == models.end()) {
            auto it2 = models.find(to_lower(wd.model_name));
            if (it2 == models.end()) {
                throw ParseError("Line " + std::to_string(wd.line_number) +
                                 ": Unknown model '" + wd.model_name + "'");
            }
            it = it2;
        }
        std::string model_type = to_lower(it->second.type);
        if (model_type != "csw") {
            throw ParseError("Line " + std::to_string(wd.line_number) +
                             ": W element references non-CSW model '" + wd.model_name + "'");
        }
        SwitchModel sm = to_switch_model(it->second);

        // Look up the sensing VSource by name
        const VSource* vs = nullptr;
        for (const auto& dev : ckt.devices()) {
            if (auto* v = dynamic_cast<const VSource*>(dev.get())) {
                if (to_lower(v->name()) == to_lower(wd.vsense_name)) {
                    vs = v;
                    break;
                }
            }
        }
        if (!vs) {
            throw ParseError("Line " + std::to_string(wd.line_number) +
                             ": W element '" + wd.name +
                             "' references unknown voltage source '" + wd.vsense_name + "'");
        }
        ckt.add_device(std::make_unique<CSwitch>(wd.name, wd.np, wd.nn, vs, sm));
    }

    // Resolve deferred LTRA (O elements) — lossy transmission lines
    std::unordered_map<std::string, std::shared_ptr<LTRAModel>> ltra_models;
    for (const auto& ol : deferred_ltras) {
        auto it = models.find(ol.model_name);
        if (it == models.end()) {
            auto it2 = models.find(to_lower(ol.model_name));
            if (it2 == models.end()) {
                throw ParseError("Line " + std::to_string(ol.line_number) +
                                 ": Unknown model '" + ol.model_name + "'");
            }
            it = it2;
        }
        std::string model_type = to_lower(it->second.type);
        if (model_type != "ltra") {
            throw ParseError("Line " + std::to_string(ol.line_number) +
                             ": O element references non-LTRA model '" + ol.model_name + "'");
        }

        // Get or create the shared LTRA model
        auto& lmodel = ltra_models[ol.model_name];
        if (!lmodel) {
            lmodel = std::make_shared<LTRAModel>();
            // Parse model parameters
            for (const auto& [key, val] : it->second.params) {
                if      (key == "r")   { lmodel->R = val; lmodel->R_given = true; }
                else if (key == "l")   { lmodel->L = val; lmodel->L_given = true; }
                else if (key == "g")   { lmodel->G = val; lmodel->G_given = true; }
                else if (key == "c")   { lmodel->C = val; lmodel->C_given = true; }
                else if (key == "len") { lmodel->len = val; lmodel->len_given = true; }
                else if (key == "rel") { lmodel->reltol = val; }
                else if (key == "abs") { lmodel->abstol = val; }
                else if (key == "compactrel") { lmodel->stLineReltol = val; }
                else if (key == "compactabs") { lmodel->stLineAbstol = val; }
                else if (key == "choprel") { lmodel->chopReltol = val; }
                else if (key == "chopabs") { lmodel->chopAbstol = val; }
                else if (key == "nocontrol") { lmodel->lteConType = LTRA_CTRL_NONE; }
                else if (key == "fullcontrol") { lmodel->lteConType = LTRA_CTRL_FULL; }
                else if (key == "halfcontrol") { lmodel->lteConType = LTRA_CTRL_HALF; }
                else if (key == "steplimit") { lmodel->stepLimit = LTRA_STEP_LIMIT; }
                else if (key == "nosteplimit") { lmodel->stepLimit = LTRA_STEP_NOLIMIT; }
                else if (key == "lininterp") { lmodel->howToInterp = LTRA_INTERP_LIN; }
                else if (key == "quadinterp") { lmodel->howToInterp = LTRA_INTERP_QUAD; }
                else if (key == "mixedinterp") { lmodel->howToInterp = LTRA_INTERP_MIXED; }
                else if (key == "truncnr") { lmodel->truncNR = true; }
                else if (key == "truncdontcut") { lmodel->truncDontCut = true; }
            }
            if (!lmodel->setup(1e-3, 1e-12)) {
                throw ParseError("Line " + std::to_string(ol.line_number) +
                                 ": Invalid LTRA model parameters for '" + ol.model_name + "'");
            }
        }

        auto ltra_dev = std::make_unique<LossyTransmissionLine>(
            ol.name, ol.p1p, ol.p1n, ol.p2p, ol.p2n, lmodel);

        // Apply initial conditions if given
        if (ol.ic_v1_given) ltra_dev->set_ic_v1(ol.ic_v1);
        if (ol.ic_i1_given) ltra_dev->set_ic_i1(ol.ic_i1);
        if (ol.ic_v2_given) ltra_dev->set_ic_v2(ol.ic_v2);
        if (ol.ic_i2_given) ltra_dev->set_ic_i2(ol.ic_i2);

        ckt.add_device(std::move(ltra_dev));
    }

    // Resolve deferred ASRC (B elements) — find VSource pointers for I() refs
    for (auto& bd : deferred_asrcs) {
        const auto& refs = bd.expr.var_refs();
        int nv = bd.expr.num_vars();
        std::vector<const VSource*> vsource_ptrs(nv, nullptr);

        for (int i = 0; i < nv; ++i) {
            if (refs[i].kind == asrc::VarKind::BRANCH_CURRENT &&
                !bd.vsrc_names[i].empty()) {
                const VSource* vs = nullptr;
                for (const auto& dev : ckt.devices()) {
                    if (auto* v = dynamic_cast<const VSource*>(dev.get())) {
                        if (to_lower(v->name()) == to_lower(bd.vsrc_names[i])) {
                            vs = v;
                            break;
                        }
                    }
                }
                if (!vs) {
                    throw ParseError("Line " + std::to_string(bd.line_number) +
                                     ": B element '" + bd.name +
                                     "' references unknown voltage source '" +
                                     bd.vsrc_names[i] + "' in I()");
                }
                vsource_ptrs[i] = vs;
            }
        }

        auto asrc_dev = std::make_unique<ASRCDevice>(
            bd.name, bd.np, bd.nn, bd.mode,
            std::move(bd.expr),
            std::move(bd.node_indices),
            std::move(bd.node_indices2),
            std::move(vsource_ptrs));

        // Apply temperature coefficient parameters
        asrc_dev->set_tc1(bd.tc1);
        asrc_dev->set_tc2(bd.tc2);
        if (bd.temp > 0) asrc_dev->set_temp(bd.temp);
        asrc_dev->set_dtemp(bd.dtemp);

        ckt.add_device(std::move(asrc_dev));
    }
}

// Helper: resolve a filename token (possibly quoted) from a line.
// pos points to the start of the filename token in `line`.
// Returns the unquoted filename and advances pos past it.
static std::string parse_filename_token(const std::string& line, size_t& pos) {
    if (pos >= line.size()) return "";
    char quote_char = line[pos];
    std::string filename;
    if (quote_char == '"' || quote_char == '\'') {
        ++pos;
        size_t end = line.find(quote_char, pos);
        if (end == std::string::npos) {
            throw ParseError("Unterminated quoted filename");
        }
        filename = line.substr(pos, end - pos);
        pos = end + 1;
    } else {
        size_t end = pos;
        while (end < line.size() && !std::isspace(static_cast<unsigned char>(line[end])))
            ++end;
        filename = line.substr(pos, end - pos);
        pos = end;
    }
    return filename;
}

// Helper: open, canonicalise and read a library/include file.
// Returns {canonical_path, file_content}.
static std::pair<std::filesystem::path, std::string>
open_lib_file(const std::string& filename,
              const std::string& base_dir,
              const std::string& directive_label) {
    std::filesystem::path fpath(filename);
    if (fpath.is_relative()) {
        fpath = std::filesystem::path(base_dir) / fpath;
    }
    std::filesystem::path canonical_path;
    try {
        canonical_path = std::filesystem::canonical(fpath);
    } catch (const std::filesystem::filesystem_error&) {
        throw ParseError(directive_label + ": cannot open file: " + fpath.string());
    }
    std::ifstream ifs(canonical_path);
    if (!ifs.is_open()) {
        throw ParseError(directive_label + ": cannot open file: " + canonical_path.string());
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return {canonical_path, oss.str()};
}

std::string NetlistParser::resolve_includes(const std::string& content,
                                             const std::string& base_dir,
                                             std::set<std::string>& include_stack) {
    std::ostringstream result;
    std::istringstream input(content);
    std::string line;

    while (std::getline(input, line)) {
        // Strip leading whitespace to find the directive keyword
        size_t start = 0;
        while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start])))
            ++start;

        // Build lowercase version from the non-whitespace start
        std::string lower_line = line.substr(start);
        std::transform(lower_line.begin(), lower_line.end(), lower_line.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        // ----------------------------------------------------------------
        // .include filename
        // ----------------------------------------------------------------
        bool is_include = false;
        size_t filename_start = 0;
        if (lower_line.size() >= 8 && lower_line.substr(0, 8) == ".include") {
            if (lower_line.size() == 8 || std::isspace(static_cast<unsigned char>(lower_line[8]))) {
                is_include = true;
                filename_start = start + 8; // position in original line after ".include"
            }
        } else if (lower_line.size() >= 4 && lower_line.substr(0, 4) == ".inc") {
            if (lower_line.size() == 4 || std::isspace(static_cast<unsigned char>(lower_line[4]))) {
                is_include = true;
                filename_start = start + 4;
            }
        }

        if (is_include) {
            // Skip whitespace after ".include"
            size_t pos = filename_start;
            while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])))
                ++pos;

            if (pos >= line.size()) {
                throw ParseError(".include directive missing filename");
            }

            std::string filename = parse_filename_token(line, pos);
            if (filename.empty()) {
                throw ParseError(".include directive has empty filename");
            }

            auto [canonical_path, file_content] = open_lib_file(filename, base_dir, ".include");
            std::string canonical_str = canonical_path.string();
            if (include_stack.count(canonical_str)) {
                throw ParseError(".include: circular include detected for file: " + canonical_str);
            }

            include_stack.insert(canonical_str);
            std::string included_base_dir = canonical_path.parent_path().string();
            std::string expanded = resolve_includes(file_content, included_base_dir, include_stack);
            include_stack.erase(canonical_str);

            result << expanded;
            if (!expanded.empty() && expanded.back() != '\n') result << '\n';
            continue;
        }

        // ----------------------------------------------------------------
        // .lib  — two forms:
        //   3+ tokens: .lib filename section  => library call (include section)
        //   2 tokens:  .lib section_name      => section start delimiter (skip at top level)
        // Also skip bare .endl lines at top level.
        // ----------------------------------------------------------------
        bool is_lib = false;
        size_t lib_rest_start = 0;
        if (lower_line.size() >= 4 && lower_line.substr(0, 4) == ".lib") {
            if (lower_line.size() == 4 || std::isspace(static_cast<unsigned char>(lower_line[4]))) {
                is_lib = true;
                lib_rest_start = start + 4; // position in original line after ".lib"
            }
        }

        if (is_lib) {
            // Collect whitespace-separated tokens from rest of line
            std::string rest = line.substr(lib_rest_start);
            std::vector<std::string> toks;
            {
                std::istringstream ss(rest);
                std::string tok;
                while (ss >> tok) toks.push_back(tok);
            }

            if (toks.size() >= 2) {
                // .lib filename section — library call
                std::string filename_tok = toks[0];
                // The filename token may be quoted; parse it properly from the rest string
                size_t pos2 = 0;
                // Skip leading whitespace in rest
                while (pos2 < rest.size() && std::isspace(static_cast<unsigned char>(rest[pos2])))
                    ++pos2;
                std::string filename = parse_filename_token(rest, pos2);

                // The section name is the next non-whitespace token after the filename
                while (pos2 < rest.size() && std::isspace(static_cast<unsigned char>(rest[pos2])))
                    ++pos2;
                if (pos2 >= rest.size()) {
                    // Only one token after .lib => section delimiter, skip it
                    continue;
                }
                size_t sec_end = pos2;
                while (sec_end < rest.size() && !std::isspace(static_cast<unsigned char>(rest[sec_end])))
                    ++sec_end;
                std::string section = rest.substr(pos2, sec_end - pos2);

                if (section.empty()) {
                    // Treated as a section delimiter (only filename given) — skip
                    continue;
                }

                // Resolve the library file
                auto [canonical_path, file_content] = open_lib_file(filename, base_dir, ".lib");
                std::string canonical_str = canonical_path.string();
                if (include_stack.count(canonical_str)) {
                    throw ParseError(".lib: circular include detected for file: " + canonical_str);
                }

                // Extract the named section from the library file
                std::string section_content = extract_lib_section(file_content, section);

                // Recursively resolve includes/libs within the extracted section
                include_stack.insert(canonical_str);
                std::string lib_base_dir = canonical_path.parent_path().string();
                std::string expanded = resolve_includes(section_content, lib_base_dir, include_stack);
                include_stack.erase(canonical_str);

                result << expanded;
                if (!expanded.empty() && expanded.back() != '\n') result << '\n';
            }
            // 0 or 1 tokens after .lib => bare ".lib" or ".lib section_name" delimiter — skip
            continue;
        }

        // ----------------------------------------------------------------
        // .endl — section end delimiter; skip at top level
        // ----------------------------------------------------------------
        bool is_endl = false;
        if (lower_line.size() >= 5 && lower_line.substr(0, 5) == ".endl") {
            if (lower_line.size() == 5 || std::isspace(static_cast<unsigned char>(lower_line[5]))) {
                is_endl = true;
            }
        }
        if (is_endl) {
            // Skip .endl lines that appear at the top-level (outside a lib section context)
            continue;
        }

        result << line << '\n';
    }

    return result.str();
}

SpiceDialect NetlistParser::detect_dialect(const std::string& content) const {
    std::string upper = content;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    for (const auto* kw : {"PARAMS:", "AKO:", "VALUE=", "OPTIONAL:", "TEXT:"}) {
        if (upper.find(kw) != std::string::npos)
            return SpiceDialect::PSPICE;
    }

    for (const auto* kw : {" VSWITCH ", " ISWITCH ", " RES ", " CAP ", " IND "}) {
        if (upper.find(kw) != std::string::npos)
            return SpiceDialect::PSPICE;
    }

    return SpiceDialect::NGSPICE;
}

Circuit NetlistParser::parse_file(const std::string& filepath) {
    std::ifstream ifs(filepath);
    if (!ifs.is_open()) {
        throw ParseError("Cannot open file: " + filepath);
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();

    // Resolve .include directives relative to the file's directory
    std::string base_dir = std::filesystem::path(filepath).parent_path().string();
    if (base_dir.empty()) {
        base_dir = ".";
    }
    std::set<std::string> include_stack;
    // Add the top-level file to the include stack to detect self-includes
    try {
        include_stack.insert(std::filesystem::canonical(filepath).string());
    } catch (const std::filesystem::filesystem_error&) {
        // If canonical fails (shouldn't happen since we already opened the file), just proceed
        include_stack.insert(std::filesystem::absolute(filepath).string());
    }
    std::string expanded = resolve_includes(oss.str(), base_dir, include_stack);

    return parse(expanded);
}

// ---------------------------------------------------------------------------
// load_definitions — read a file, resolve includes, extract definitions only
// ---------------------------------------------------------------------------
DefinitionSet NetlistParser::load_definitions(const std::string& filepath) {
    std::ifstream ifs(filepath);
    if (!ifs.is_open())
        throw ParseError("Cannot open file: " + filepath);
    std::ostringstream oss;
    oss << ifs.rdbuf();

    std::string base_dir = std::filesystem::path(filepath).parent_path().string();
    if (base_dir.empty()) base_dir = ".";

    std::set<std::string> include_stack;
    try {
        include_stack.insert(std::filesystem::canonical(filepath).string());
    } catch (const std::filesystem::filesystem_error&) {
        include_stack.insert(std::filesystem::absolute(filepath).string());
    }
    std::string expanded = resolve_includes(oss.str(), base_dir, include_stack);

    // Auto-detect dialect if needed
    SpiceDialect d = dialect_;
    if (d == SpiceDialect::AUTO)
        d = detect_dialect(expanded);

    // Tokenize
    auto lines = tokenize(expanded);

    // Create temporary circuit and state for extraction passes
    Circuit tmp_ckt;
    std::unordered_map<std::string, SubcircuitDef> tmp_subdefs;
    ParseState state(tmp_ckt, tmp_subdefs);
    state.lines = std::move(lines);
    state.dialect = d;

    // Run only the definition-extraction passes
    pass0_extract_subcircuits(state);      // extracts .subckt/.ends
    pass025_resolve_funcs_params(state);   // .func + .param resolution
    pass1_collect_models_params(state);    // .model parsing + AKO resolution

    // Package results
    DefinitionSet defs;
    defs.subcircuit_defs = std::move(state.subcircuit_defs);
    defs.models = std::move(state.models);
    defs.func_defs = std::move(state.func_defs);
    defs.params = std::move(state.params);
    return defs;
}

// ---------------------------------------------------------------------------
// expand_subcircuit_into — instantiate a subcircuit into an existing circuit
// ---------------------------------------------------------------------------
void NetlistParser::expand_subcircuit_into(
    Circuit& ckt,
    const std::string& instance_name,
    const std::string& subckt_name,
    const std::vector<std::string>& port_nodes,
    const DefinitionSet& defs,
    const std::unordered_map<std::string, std::string>& instance_params) {

    // Look up subcircuit (case-insensitive, lowercase key)
    std::string key = to_lower(subckt_name);
    auto it = defs.subcircuit_defs.find(key);
    if (it == defs.subcircuit_defs.end())
        throw ParseError("Subcircuit not found: " + subckt_name);

    // Build a synthetic X-line
    TokenizedLine xline;
    xline.line_number = 0;
    xline.tokens.push_back("x" + instance_name);
    for (const auto& node : port_nodes)
        xline.tokens.push_back(node);
    xline.tokens.push_back(subckt_name);
    for (const auto& [k, v] : instance_params)
        xline.tokens.push_back(k + "=" + v);

    // Local copy of subcircuit_defs for ParseState reference
    std::unordered_map<std::string, SubcircuitDef> local_subdefs = defs.subcircuit_defs;

    // Expand the X-line into primitive element lines
    auto expanded_lines = expand_all_instances(
        {xline}, local_subdefs, defs.params, {});

    // Create ParseState targeting caller's circuit
    ParseState state(ckt, local_subdefs);
    state.lines = std::move(expanded_lines);
    state.dialect = dialect_;

    // Pre-populate from defs
    state.models = defs.models;
    state.params = defs.params;
    state.func_defs = defs.func_defs;

    // Run passes to parse elements and resolve deferred devices
    pass1_collect_models_params(state);  // picks up .model from expanded subcircuit body
    pass2_parse_elements(state);         // adds devices to ckt
    pass3_resolve_deferred(state);       // resolves deferred devices
}

} // namespace neospice
