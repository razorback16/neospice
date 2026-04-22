#include "parser/netlist_parser.hpp"
#include "parser/tokenizer.hpp"
#include "parser/subcircuit.hpp"
#include "parser/subcircuit_expand.hpp"
#include "parser/expression.hpp"
#include "parser/model_cards.hpp"
#include "devices/resistor.hpp"
#include "devices/resistor_model.hpp"
#include "devices/capacitor.hpp"
#include "devices/capacitor_model.hpp"
#include "devices/inductor.hpp"
#include "devices/inductor_model.hpp"
#include "devices/coupled_inductor.hpp"
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include "devices/dio/dio_device.hpp"
#include "devices/vcvs.hpp"
#include "devices/vccs.hpp"
#include "devices/ccvs.hpp"
#include "devices/cccs.hpp"
#include "devices/switch.hpp"
#include "devices/vcvs_nonlinear.hpp"
#include "devices/vccs_nonlinear.hpp"
#include "devices/ccvs_nonlinear.hpp"
#include "devices/cccs_nonlinear.hpp"
#include "devices/bsim4v7/bsim4v7_device.hpp"
#include "devices/mos1/mos1_device.hpp"
#include "devices/mos9/mos9_device.hpp"
#include "devices/mos9/mos9_model_card.hpp"
#include "devices/bsim3/bsim3_device.hpp"
#include "devices/bjt/bjt_device.hpp"
#include "devices/jfet/jfet_device.hpp"
#include "devices/hfet2/hfet2_device.hpp"
#include "devices/vbic/vbic_device.hpp"
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
struct SourceSpec {
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

SourceSpec parse_source_spec(const std::vector<std::string>& tokens, size_t start_idx) {
    SourceSpec spec;
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

    // Strip leading whitespace so the tokenizer's title-line detection works
    // correctly even when the netlist starts with newlines.
    size_t start = 0;
    while (start < netlist.size() &&
           std::isspace(static_cast<unsigned char>(netlist[start])))
        ++start;
    std::string trimmed = netlist.substr(start);

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

    auto lines = tokenize(trimmed);

    // Pass 0: extract .subckt/.ends blocks into subcircuit_defs_
    // This must run before Pass 1 and Pass 2 so subcircuit body lines are
    // not processed as top-level elements.
    {
        subcircuit_defs_.clear();
        std::vector<TokenizedLine> remaining_lines;
        remaining_lines.reserve(lines.size());

        int depth = 0;
        SubcircuitDef current_def;
        // Stack of outer defs when nesting deeper than 1
        std::vector<SubcircuitDef> def_stack;

        for (const auto& line : lines) {
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
                    subcircuit_defs_[current_def.name] = std::move(current_def);
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

        lines = std::move(remaining_lines);
    }

    // Pass 0.25: Pre-collect .func definitions, expand func calls in all
    // tokens, then collect top-level .param entries so that expansion
    // (Pass 0.5) can resolve parameter expressions like R={myR}.
    std::unordered_map<std::string, FuncDef> func_defs;
    std::unordered_map<std::string, double> global_params;
    {
        // First collect .func definitions
        for (const auto& line : lines) {
            if (line.tokens.empty()) continue;
            std::string first = to_lower(line.tokens[0]);
            if (first == ".func") {
                parse_func_def(line.tokens, func_defs);
            }
        }

        // Expand .func calls in all tokens (modifies lines in place)
        if (!func_defs.empty()) {
            for (auto& line : lines) {
                for (auto& tok : line.tokens) {
                    tok = expand_funcs(tok, func_defs);
                }
            }
        }

        // Then collect .param entries
        std::vector<std::pair<std::string, std::string>> pre_raw_params;
        for (const auto& line : lines) {
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
            global_params = resolve_params(pre_raw_params);
        }
    }

    // Pass 0.4: Collect .global node declarations.
    // Nodes listed on .global lines are shared across all subcircuit
    // hierarchies and must NOT be prefixed during expansion.
    std::unordered_set<std::string> global_nodes;
    for (const auto& line : lines) {
        if (line.tokens.empty()) continue;
        if (to_lower(line.tokens[0]) == ".global") {
            for (size_t i = 1; i < line.tokens.size(); ++i) {
                global_nodes.insert(to_lower(line.tokens[i]));
            }
        }
    }

    // Pass 0.5: Expand X instances into primitive element lines.
    // After this step, `lines` contains no X elements.
    // Always call — even when subcircuit_defs_ is empty — so that X lines
    // referencing undefined subcircuits produce a proper ParseError.
    lines = expand_all_instances(lines, subcircuit_defs_, global_params, global_nodes);

    // Storage for params and models
    std::unordered_map<std::string, double> params;
    std::unordered_map<std::string, ModelCard> models;
    std::unordered_map<std::string, ResistorModel> res_models;
    std::unordered_map<std::string, CapacitorModel> cap_models;
    std::unordered_map<std::string, InductorModel> ind_models;

    struct DeferredDiode {
        std::string name;
        std::string anode;
        std::string cathode;
        std::string model_name;
        int line_number;
        DIODevice::Geom geom;
        double ic_vd = 0.0;
        bool ic_vd_given = false;
    };
    std::vector<DeferredDiode> deferred_diodes;

    // Deferred CCVS (H elements): resolved after all VSource devices exist so
    // we can locate the sensing VSource by name and pass a pointer to CCVS.
    struct DeferredCCVS {
        std::string name;
        int32_t np, nn;
        std::string vsense_name;
        double rm;
        int line_number;
    };
    std::vector<DeferredCCVS> deferred_ccvs;

    // Deferred CCCS (F elements): resolved after all VSource devices exist so
    // we can locate the sensing VSource by name and pass a pointer to CCCS.
    struct DeferredCCCS {
        std::string name;
        int32_t np, nn;
        std::string vsense_name;
        double gain;
        double m = 1.0;
        int line_number;
    };
    std::vector<DeferredCCCS> deferred_cccs;

    // Deferred POLY CCVS (H POLY elements): resolved after all VSource devices exist.
    struct DeferredPolyCCVS {
        std::string name;
        int32_t np, nn;
        std::vector<std::string> vsense_names;
        std::vector<double> coeffs;
        int line_number;
    };
    std::vector<DeferredPolyCCVS> deferred_poly_ccvs;

    // Deferred POLY CCCS (F POLY elements): resolved after all VSource devices exist.
    struct DeferredPolyCCCS {
        std::string name;
        int32_t np, nn;
        std::vector<std::string> vsense_names;
        std::vector<double> coeffs;
        int line_number;
    };
    std::vector<DeferredPolyCCCS> deferred_poly_cccs;

    // Deferred MOSFETs: parsed M-cards are resolved in a second pass once
    // all .model cards are known.  node indices are already mapped (we have
    // access to `ckt` when scanning element lines).
    struct DeferredMosfet {
        std::string name;
        int32_t nd, ng, ns, nb;
        std::string model_name;
        BSIM4v7Device::Geom geom;
        int line_number;
        // Instance initial conditions from ic=VDS,VGS,VBS
        double ic_vds = 0.0, ic_vgs = 0.0, ic_vbs = 0.0;
        bool ic_vds_given = false, ic_vgs_given = false, ic_vbs_given = false;
    };
    std::vector<DeferredMosfet> deferred_mosfets;

    // Deferred coupled inductors (K elements): resolved after all Inductor
    // devices exist so we can locate them by name and pass pointers.
    struct DeferredCoupledInductor {
        std::string name;
        std::string l1_name;
        std::string l2_name;
        double coupling;
        int line_number;
    };
    std::vector<DeferredCoupledInductor> deferred_coupled_inductors;

    // Deferred BJTs: parsed Q-cards are resolved in a second pass once
    // all .model cards are known.
    struct DeferredBJT {
        std::string name;
        int32_t nc, nb, ne, ns;  // collector, base, emitter, substrate
        std::string model_name;
        BJTDevice::Geom geom;
        int line_number;
        double ic_vbe = 0.0, ic_vce = 0.0;
        bool ic_vbe_given = false, ic_vce_given = false;
    };
    std::vector<DeferredBJT> deferred_bjts;

    // Deferred JFETs: parsed J-cards are resolved in a second pass once
    // all .model cards are known.
    struct DeferredJFET {
        std::string name;
        std::string nd, ng, ns;  // drain, gate, source node names
        std::string model_name;
        JFETDevice::Geom geom;
        int line_number = 0;
        double ic_vds = 0, ic_vgs = 0;
        bool ic_vds_given = false, ic_vgs_given = false;
    };
    std::vector<DeferredJFET> deferred_jfets;

    // Deferred HFET2s: parsed Z-cards with level=6 (nhfet/phfet) resolved
    // in a second pass once all .model cards are known.
    struct DeferredHFET2 {
        std::string name;
        std::string nd, ng, ns;  // drain, gate, source node names
        std::string model_name;
        HFET2Device::Geom geom;
        int line_number = 0;
        double ic_vds = 0, ic_vgs = 0;
        bool ic_vds_given = false, ic_vgs_given = false;
    };
    std::vector<DeferredHFET2> deferred_hfet2s;

    // Deferred VSwitch (S elements): resolved after all .model cards are known.
    struct DeferredVSwitch {
        std::string name;
        int32_t np, nn, ncp, ncn;
        std::string model_name;
        int line_number;
    };
    std::vector<DeferredVSwitch> deferred_vswitches;

    // Deferred CSwitch (W elements): resolved after all VSource devices and .model cards are known.
    struct DeferredCSwitch {
        std::string name;
        int32_t np, nn;
        std::string vsense_name;
        std::string model_name;
        int line_number;
    };
    std::vector<DeferredCSwitch> deferred_cswitches;

    // Deferred LTRA (O elements): resolved after all .model cards are known.
    struct DeferredLTRA {
        std::string name;
        int32_t p1p, p1n, p2p, p2n;
        std::string model_name;
        int line_number;
    };
    std::vector<DeferredLTRA> deferred_ltras;

    // Deferred ASRC (B elements): the expression is compiled immediately but
    // I() references need VSource pointers resolved in a second pass.
    struct DeferredASRC {
        std::string name;
        int32_t np, nn;
        ASRCDevice::Mode mode;
        asrc::CompiledExpression expr;
        // Per-variable resolved data (filled at parse time for V() refs)
        std::vector<int32_t> node_indices;   // -1 = ground, -2 = TIME
        std::vector<int32_t> node_indices2;  // second node for V(n1,n2)
        // Names of vsources for I() refs (resolved later)
        std::vector<std::string> vsrc_names; // empty string if not I() ref
        int line_number;
    };
    std::vector<DeferredASRC> deferred_asrcs;

    // Pass 1: collect .model, .func, and .param cards
    // Re-collect .func from post-expansion lines (subcircuit bodies may define .func)
    for (const auto& line : lines) {
        if (line.tokens.empty()) continue;
        std::string first = to_lower(line.tokens[0]);
        if (first == ".func") {
            parse_func_def(line.tokens, func_defs);
        }
    }

    // Expand .func calls in all tokens (post-subcircuit-expansion lines)
    if (!func_defs.empty()) {
        for (auto& line : lines) {
            for (auto& tok : line.tokens) {
                tok = expand_funcs(tok, func_defs);
            }
        }
    }

    std::vector<std::pair<std::string, std::string>> raw_params;
    for (const auto& line : lines) {
        if (line.tokens.empty()) continue;
        std::string first = to_lower(line.tokens[0]);

        if (first == ".model") {
            auto card = parse_model_card(line.tokens);
            models[card.name] = card;
            if (card.type == "r") {
                res_models[to_lower(card.name)] = to_resistor_model(card);
            } else if (card.type == "c") {
                cap_models[to_lower(card.name)] = to_capacitor_model(card);
            } else if (card.type == "l") {
                ind_models[to_lower(card.name)] = to_inductor_model(card);
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
    // Resolve all .param definitions in dependency order (handles forward references)
    if (!raw_params.empty()) {
        params = resolve_params(raw_params);
    }

    // Pass 2: parse element lines and dot commands
    for (const auto& line : lines) {
        if (line.tokens.empty()) continue;
        const auto& tokens = line.tokens;
        std::string first = to_lower(tokens[0]);

        // Dot commands
        if (first[0] == '.') {
            if (first == ".op") {
                AnalysisCommand cmd;
                cmd.type = AnalysisCommand::OP;
                ckt.analyses.push_back(cmd);
            } else if (first == ".tran") {
                if (tokens.size() < 3) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .tran requires tstep and tstop");
                }
                AnalysisCommand cmd;
                cmd.type = AnalysisCommand::TRAN;
                cmd.tran_tstep = parse_spice_number(tokens[1]);
                cmd.tran_tstop = parse_spice_number(tokens[2]);
                // Check remaining tokens for UIC keyword
                for (size_t k = 3; k < tokens.size(); ++k) {
                    if (to_lower(tokens[k]) == "uic") {
                        cmd.tran_uic = true;
                    }
                }
                ckt.analyses.push_back(cmd);
            } else if (first == ".ac") {
                if (tokens.size() < 5) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .ac requires mode npoints fstart fstop");
                }
                AnalysisCommand cmd;
                cmd.type = AnalysisCommand::AC;
                std::string mode = to_lower(tokens[1]);
                if (mode == "dec") cmd.ac_mode = AnalysisCommand::DEC;
                else if (mode == "oct") cmd.ac_mode = AnalysisCommand::OCT;
                else if (mode == "lin") cmd.ac_mode = AnalysisCommand::LIN;
                else throw ParseError("Unknown AC mode: " + tokens[1]);
                cmd.ac_npoints = static_cast<int>(parse_spice_number(tokens[2]));
                cmd.ac_fstart = parse_spice_number(tokens[3]);
                cmd.ac_fstop = parse_spice_number(tokens[4]);
                ckt.analyses.push_back(cmd);
            } else if (first == ".noise") {
                // .noise V(out) Vin dec 10 1 1e9
                if (tokens.size() < 7) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .noise requires V(output) input_source mode npoints fstart fstop");
                }
                AnalysisCommand cmd;
                cmd.type = AnalysisCommand::NOISE;

                // Parse output spec: V(node) or v(node)
                std::string out_spec = tokens[1];
                std::string out_lower = to_lower(out_spec);
                if (out_lower.size() > 3 && out_lower.substr(0, 2) == "v(" && out_lower.back() == ')') {
                    cmd.noise_output = out_lower.substr(2, out_lower.size() - 3);
                } else {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .noise output must be V(node)");
                }

                // Input source name
                cmd.noise_input_src = tokens[2];

                // Frequency sweep parameters (reuse ac_mode, ac_npoints, ac_fstart, ac_fstop)
                std::string mode = to_lower(tokens[3]);
                if (mode == "dec") cmd.ac_mode = AnalysisCommand::DEC;
                else if (mode == "oct") cmd.ac_mode = AnalysisCommand::OCT;
                else if (mode == "lin") cmd.ac_mode = AnalysisCommand::LIN;
                else throw ParseError("Line " + std::to_string(line.line_number) +
                                      ": Unknown noise sweep mode: " + tokens[3]);
                cmd.ac_npoints = static_cast<int>(parse_spice_number(tokens[4]));
                cmd.ac_fstart = parse_spice_number(tokens[5]);
                cmd.ac_fstop = parse_spice_number(tokens[6]);
                ckt.analyses.push_back(cmd);
            } else if (first == ".dc") {
                // .dc Vsrc1 start1 stop1 step1 [Vsrc2 start2 stop2 step2]
                // Each sweep group is 4 tokens: source_name start stop step
                if (tokens.size() < 5) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .dc requires source start stop step");
                }
                AnalysisCommand cmd;
                cmd.type = AnalysisCommand::DC_SWEEP;
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
                    cmd.dc_sweep_params.push_back(sp);
                    i += 4;
                    if (cmd.dc_sweep_params.size() >= 2) break; // support at most 2 sweeps
                }
                if (cmd.dc_sweep_params.empty()) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .dc requires at least one sweep group");
                }
                ckt.analyses.push_back(cmd);
            } else if (first == ".options") {
                for (size_t i = 1; i < tokens.size(); ++i) {
                    auto eq_pos = tokens[i].find('=');
                    if (eq_pos == std::string::npos) continue;
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
                        int32_t idx = ckt.node(node_name);
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
                        int32_t idx = ckt.node(node_name);
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
                AnalysisCommand cmd;
                cmd.type = AnalysisCommand::TF;
                cmd.tf_output = to_lower(tokens[1]);
                cmd.tf_input_src = to_lower(tokens[2]);
                ckt.analyses.push_back(cmd);
            } else if (first == ".sens") {
                // .sens output_var
                // e.g., .sens V(out)
                if (tokens.size() < 2) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .sens requires output variable");
                }
                AnalysisCommand cmd;
                cmd.type = AnalysisCommand::SENS;
                cmd.sens_output = to_lower(tokens[1]);
                ckt.analyses.push_back(cmd);
            } else if (first == ".pz") {
                // .pz node1 node2 node3 node4 VOL|CUR POL|ZER|PZ
                if (tokens.size() < 7) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .pz requires 6 arguments: n1 n2 n3 n4 VOL|CUR POL|ZER|PZ");
                }
                AnalysisCommand cmd;
                cmd.type = AnalysisCommand::PZ;
                cmd.pz_in_pos  = to_lower(tokens[1]);
                cmd.pz_in_neg  = to_lower(tokens[2]);
                cmd.pz_out_pos = to_lower(tokens[3]);
                cmd.pz_out_neg = to_lower(tokens[4]);
                std::string transfer = to_lower(tokens[5]);
                if (transfer == "vol")
                    cmd.pz_transfer = PZTransferType::VOLTAGE;
                else if (transfer == "cur")
                    cmd.pz_transfer = PZTransferType::CURRENT;
                else
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .pz transfer type must be VOL or CUR");
                std::string pz = to_lower(tokens[6]);
                if (pz == "pol")      cmd.pz_type = PZType::POLES;
                else if (pz == "zer") cmd.pz_type = PZType::ZEROS;
                else if (pz == "pz")  cmd.pz_type = PZType::BOTH;
                else
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": .pz type must be POL, ZER, or PZ");
                ckt.analyses.push_back(cmd);
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
            int32_t np = ckt.node(tokens[1]);
            int32_t nn = ckt.node(tokens[2]);
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
            int32_t np = ckt.node(tokens[1]);
            int32_t nn = ckt.node(tokens[2]);
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
            int32_t np = ckt.node(tokens[1]);
            int32_t nn = ckt.node(tokens[2]);

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
            int32_t np = ckt.node(tokens[1]);
            int32_t nn = ckt.node(tokens[2]);

            SourceSpec spec = parse_source_spec(tokens, 3);
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
            int32_t np = ckt.node(tokens[1]);
            int32_t nn = ckt.node(tokens[2]);

            SourceSpec spec = parse_source_spec(tokens, 3);
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

        } else if (elem_type == 'd') {
            // D name anode cathode modelname [area=.. pj=.. m=.. ic=..]
            if (tokens.size() < 4) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": Diode requires name, anode, cathode, modelname");
            }
            DeferredDiode dd;
            dd.name = tokens[0];
            dd.anode = tokens[1];
            dd.cathode = tokens[2];
            dd.model_name = tokens[3];
            dd.line_number = line.line_number;
            for (size_t i = 4; i < tokens.size(); ++i) {
                auto eq = tokens[i].find('=');
                if (eq == std::string::npos) {
                    // Bare number after model name = area factor
                    try {
                        dd.geom.area = std::stod(tokens[i]);
                    } catch (...) {}
                    continue;
                }
                std::string key = to_lower(tokens[i].substr(0, eq));
                double v = std::stod(tokens[i].substr(eq + 1));
                if (key == "area")     dd.geom.area = v;
                else if (key == "pj")  dd.geom.pj = v;
                else if (key == "w")   dd.geom.w = v;
                else if (key == "l")   dd.geom.l = v;
                else if (key == "m")   dd.geom.m = v;
                else if (key == "ic")  { dd.ic_vd = v; dd.ic_vd_given = true; }
            }
            deferred_diodes.push_back(std::move(dd));

        } else if (elem_type == 'm') {
            // M name nd ng ns nb modelname [W=.. L=.. NF=.. AD=.. AS=.. PD=..
            //                                PS=.. NRD=.. NRS=.. SA=.. SB=.. SD=..]
            if (tokens.size() < 6) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": M card requires name, nd, ng, ns, nb, modelname");
            }
            DeferredMosfet m;
            m.name        = tokens[0];
            m.nd          = ckt.node(tokens[1]);
            m.ng          = ckt.node(tokens[2]);
            m.ns          = ckt.node(tokens[3]);
            m.nb          = ckt.node(tokens[4]);
            m.model_name  = tokens[5];
            m.line_number = line.line_number;
            for (size_t i = 6; i < tokens.size(); ++i) {
                auto eq = tokens[i].find('=');
                if (eq == std::string::npos) continue;
                std::string key = to_lower(tokens[i].substr(0, eq));
                if (key == "ic") {
                    // ic=VDS,VGS,VBS  or  ic=VDS,VGS  or  ic=VDS
                    std::string valstr = tokens[i].substr(eq + 1);
                    std::vector<double> icvals;
                    size_t start = 0;
                    while (start < valstr.size()) {
                        size_t comma = valstr.find(',', start);
                        if (comma == std::string::npos) comma = valstr.size();
                        std::string field = valstr.substr(start, comma - start);
                        if (!field.empty())
                            icvals.push_back(parse_spice_number(field));
                        start = comma + 1;
                    }
                    if (icvals.size() >= 1) { m.ic_vds = icvals[0]; m.ic_vds_given = true; }
                    if (icvals.size() >= 2) { m.ic_vgs = icvals[1]; m.ic_vgs_given = true; }
                    if (icvals.size() >= 3) { m.ic_vbs = icvals[2]; m.ic_vbs_given = true; }
                    continue;
                }
                double val = parse_spice_number(tokens[i].substr(eq + 1));
                if      (key == "w")   m.geom.W   = val;
                else if (key == "l")   m.geom.L   = val;
                else if (key == "nf")  m.geom.NF  = val;
                else if (key == "ad")  m.geom.AD  = val;
                else if (key == "as")  m.geom.AS  = val;
                else if (key == "pd")  m.geom.PD  = val;
                else if (key == "ps")  m.geom.PS  = val;
                else if (key == "nrd") m.geom.NRD = val;
                else if (key == "nrs") m.geom.NRS = val;
                else if (key == "sa")  m.geom.SA  = val;
                else if (key == "sb")  m.geom.SB  = val;
                else if (key == "sd")  m.geom.SD  = val;
                else if (key == "m")   m.geom.M   = val;
                // Silently ignore other unknown M-card keys — UCB supports ~20 more
                // (e.g., TEMP, DTEMP, RBDB, RBSB, ...) that
                // default cleanly in BSIM4setup.
            }
            deferred_mosfets.push_back(std::move(m));

        } else if (elem_type == 'e') {
            // E name np nn [POLY(N) cp1 cn1 ... coeffs | TABLE {V(in)} = (x,y)... | nc+ nc- gain]
            if (tokens.size() < 4) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": VCVS requires name, np, nn, and source specification");
            }
            std::string name = tokens[0];
            int32_t np  = ckt.node(tokens[1]);
            int32_t nn  = ckt.node(tokens[2]);

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
                    int32_t cp = ckt.node(tokens[idx]);
                    int32_t cn = ckt.node(tokens[idx + 1]);
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

            } else if (tok3 == "table") {
                // TABLE {V(in)} = (x1,y1) (x2,y2) ...
                // Parse control expression: token[4] should be "{V(node)}" or similar
                // For now support: TABLE {V(node)} = (x,y) (x,y) ...
                if (tokens.size() < 6) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": TABLE VCVS requires control expression and table points");
                }
                // Extract node name from {V(node)} at token[4]
                std::string ctrl_expr = tokens[4];
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
                            ctrl_pos = ckt.node(node_name.substr(0, comma));
                            ctrl_neg = ckt.node(node_name.substr(comma + 1));
                        } else {
                            ctrl_pos = ckt.node(node_name);
                            // ctrl_neg stays GROUND_INTERNAL
                        }
                    }
                }
                // Skip "=" token if present at token[5]
                size_t idx = 5;
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

            } else {
                // Linear form: E name np nn nc+ nc- gain
                if (tokens.size() < 6) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": VCVS requires name, np, nn, nc+, nc-, gain");
                }
                int32_t ncp = ckt.node(tokens[3]);
                int32_t ncn = ckt.node(tokens[4]);
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
            int32_t np  = ckt.node(tokens[1]);
            int32_t nn  = ckt.node(tokens[2]);

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
                    int32_t cp = ckt.node(tokens[idx]);
                    int32_t cn = ckt.node(tokens[idx + 1]);
                    ctrl_pairs.push_back({cp, cn});
                    idx += 2;
                }
                std::vector<double> coeffs;
                for (; idx < tokens.size(); ++idx) {
                    coeffs.push_back(parse_spice_number(tokens[idx]));
                }
                ckt.add_device(std::make_unique<NonlinearVCCS>(
                    name, np, nn, std::move(ctrl_pairs), std::move(coeffs)));

            } else if (tok3g == "table") {
                // TABLE {V(node)} = (x,y) ...
                if (tokens.size() < 6) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": TABLE VCCS requires control expression and table points");
                }
                std::string ctrl_expr = tokens[4];
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
                            ctrl_pos = ckt.node(node_name.substr(0, comma));
                            ctrl_neg = ckt.node(node_name.substr(comma + 1));
                        } else {
                            ctrl_pos = ckt.node(node_name);
                        }
                    }
                }
                size_t idx = 5;
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

            } else {
                // Linear form: G name np nn nc+ nc- gm
                if (tokens.size() < 6) {
                    throw ParseError("Line " + std::to_string(line.line_number) +
                                     ": VCCS requires name, np, nn, nc+, nc-, gm");
                }
                int32_t ncp = ckt.node(tokens[3]);
                int32_t ncn = ckt.node(tokens[4]);
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
                hpd.np = ckt.node(tokens[1]);
                hpd.nn = ckt.node(tokens[2]);
                hpd.vsense_names = std::move(vsense_names);
                hpd.coeffs = std::move(coeffs);
                hpd.line_number = line.line_number;
                deferred_poly_ccvs.push_back(std::move(hpd));
            } else {
                // Linear form: H name np nn Vsense transresistance
                DeferredCCVS hd;
                hd.name        = tokens[0];
                hd.np          = ckt.node(tokens[1]);
                hd.nn          = ckt.node(tokens[2]);
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
                fpd.np = ckt.node(tokens[1]);
                fpd.nn = ckt.node(tokens[2]);
                fpd.vsense_names = std::move(vsense_names);
                fpd.coeffs = std::move(coeffs);
                fpd.line_number = line.line_number;
                deferred_poly_cccs.push_back(std::move(fpd));
            } else {
                // Linear form: F name np nn Vsense gain
                DeferredCCCS fd;
                fd.name        = tokens[0];
                fd.np          = ckt.node(tokens[1]);
                fd.nn          = ckt.node(tokens[2]);
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

        } else if (elem_type == 'q') {
            // Q name nc nb ne [ns] modelname [area=X] [areab=X] [areac=X] [m=X] [ic=VBE,VCE] [off]
            if (tokens.size() < 5) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": Q card requires at least name, nc, nb, ne, model");
            }
            DeferredBJT q;
            q.name = tokens[0];
            q.nb = ckt.node(tokens[2]);
            q.ne = ckt.node(tokens[3]);
            q.line_number = line.line_number;

            // Determine if token[4] is a substrate node or model name.
            // Heuristic: if token[4] matches a known .model name, treat it as model.
            // Otherwise treat as substrate node and look for model as next token.
            std::string tok4 = tokens[4];
            std::string tok4_lower = to_lower(tok4);
            // Case-insensitive model name lookup
            bool is_model_name = false;
            for (const auto& [mname, _] : models) {
                if (to_lower(mname) == tok4_lower) {
                    is_model_name = true;
                    break;
                }
            }
            size_t param_start;
            if (is_model_name) {
                q.nc = ckt.node(tokens[1]);
                q.ns = ckt.node("0");  // default substrate = ground
                q.model_name = tok4;
                param_start = 5;
            } else if (tokens.size() >= 6) {
                q.nc = ckt.node(tokens[1]);
                q.ns = ckt.node(tok4);
                q.model_name = tokens[5];
                param_start = 6;
            } else {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": Q card: cannot determine model name");
            }

            // Parse optional parameters: area, areab, areac, m, ic, off
            for (size_t i = param_start; i < tokens.size(); ++i) {
                auto eq = tokens[i].find('=');
                if (eq == std::string::npos) {
                    std::string lower = to_lower(tokens[i]);
                    if (lower == "off") continue; // ignore OFF flag
                    // Bare number = area factor (legacy SPICE2 syntax)
                    try {
                        q.geom.area = parse_spice_number(tokens[i]);
                        q.geom.area_given = true;
                    } catch (...) {}
                    continue;
                }
                std::string key = to_lower(tokens[i].substr(0, eq));
                std::string valstr = tokens[i].substr(eq + 1);
                if (key == "ic") {
                    // ic=VBE,VCE
                    std::vector<double> icvals;
                    size_t start = 0;
                    while (start < valstr.size()) {
                        size_t comma = valstr.find(',', start);
                        if (comma == std::string::npos) comma = valstr.size();
                        std::string field = valstr.substr(start, comma - start);
                        if (!field.empty()) icvals.push_back(parse_spice_number(field));
                        start = comma + 1;
                    }
                    if (icvals.size() >= 1) { q.ic_vbe = icvals[0]; q.ic_vbe_given = true; }
                    if (icvals.size() >= 2) { q.ic_vce = icvals[1]; q.ic_vce_given = true; }
                    continue;
                }
                double val = parse_spice_number(valstr);
                if (key == "area") { q.geom.area = val; q.geom.area_given = true; }
                else if (key == "areab") { q.geom.areab = val; q.geom.areab_given = true; }
                else if (key == "areac") { q.geom.areac = val; q.geom.areac_given = true; }
                else if (key == "m") { q.geom.m = val; q.geom.m_given = true; }
            }
            deferred_bjts.push_back(std::move(q));

        } else if (elem_type == 'j') {
            // J name drain gate source model [area] [off] [ic=VDS,VGS]
            if (tokens.size() < 5) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": J card requires at least name, nd, ng, ns, model");
            }
            DeferredJFET j;
            j.name = tokens[0];
            j.nd = tokens[1];
            j.ng = tokens[2];
            j.ns = tokens[3];
            j.model_name = tokens[4];
            j.line_number = line.line_number;
            // Parse remaining: area=, m=, ic=, off, or bare area number
            for (size_t i = 5; i < tokens.size(); ++i) {
                auto eq = tokens[i].find('=');
                if (eq != std::string::npos) {
                    std::string key = to_lower(tokens[i].substr(0, eq));
                    std::string valstr = tokens[i].substr(eq + 1);
                    if (key == "area") {
                        j.geom.area = parse_spice_number(valstr);
                        j.geom.area_given = true;
                    } else if (key == "m") {
                        j.geom.m = parse_spice_number(valstr);
                        j.geom.m_given = true;
                    } else if (key == "ic") {
                        // ic=VDS,VGS
                        std::vector<double> icvals;
                        size_t start = 0;
                        while (start < valstr.size()) {
                            size_t comma = valstr.find(',', start);
                            if (comma == std::string::npos) comma = valstr.size();
                            std::string field = valstr.substr(start, comma - start);
                            if (!field.empty()) icvals.push_back(parse_spice_number(field));
                            start = comma + 1;
                        }
                        if (icvals.size() >= 1) { j.ic_vds = icvals[0]; j.ic_vds_given = true; }
                        if (icvals.size() >= 2) { j.ic_vgs = icvals[1]; j.ic_vgs_given = true; }
                    }
                } else {
                    std::string lower = to_lower(tokens[i]);
                    if (lower == "off") continue; // ignore OFF flag
                    // Bare number = area factor (legacy SPICE2 syntax)
                    if (!j.geom.area_given) {
                        try {
                            j.geom.area = parse_spice_number(tokens[i]);
                            j.geom.area_given = true;
                        } catch (...) {}
                    }
                }
            }
            deferred_jfets.push_back(std::move(j));

        } else if (elem_type == 'z') {
            // Z name drain gate source model [L=val] [W=val] [M=val] [off] [ic=VDS,VGS]
            if (tokens.size() < 5) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": Z card requires at least name, nd, ng, ns, model");
            }
            DeferredHFET2 z;
            z.name = tokens[0];
            z.nd = tokens[1];
            z.ng = tokens[2];
            z.ns = tokens[3];
            z.model_name = tokens[4];
            z.line_number = line.line_number;
            // Parse remaining: L=, W=, M=, ic=, off
            for (size_t i = 5; i < tokens.size(); ++i) {
                auto eq = tokens[i].find('=');
                if (eq != std::string::npos) {
                    std::string key = to_lower(tokens[i].substr(0, eq));
                    std::string valstr = tokens[i].substr(eq + 1);
                    if (key == "l") {
                        z.geom.L = parse_spice_number(valstr);
                    } else if (key == "w") {
                        z.geom.W = parse_spice_number(valstr);
                    } else if (key == "m") {
                        z.geom.M = parse_spice_number(valstr);
                    } else if (key == "ic") {
                        // ic=VDS,VGS
                        std::vector<double> icvals;
                        size_t start = 0;
                        while (start < valstr.size()) {
                            size_t comma = valstr.find(',', start);
                            if (comma == std::string::npos) comma = valstr.size();
                            std::string field = valstr.substr(start, comma - start);
                            if (!field.empty()) icvals.push_back(parse_spice_number(field));
                            start = comma + 1;
                        }
                        if (icvals.size() >= 1) { z.ic_vds = icvals[0]; z.ic_vds_given = true; }
                        if (icvals.size() >= 2) { z.ic_vgs = icvals[1]; z.ic_vgs_given = true; }
                    }
                } else {
                    std::string lower = to_lower(tokens[i]);
                    if (lower == "off") continue; // ignore OFF flag
                }
            }
            deferred_hfet2s.push_back(std::move(z));

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
            int32_t tp1p = ckt.node(tokens[1]);
            int32_t tp1n = ckt.node(tokens[2]);
            int32_t tp2p = ckt.node(tokens[3]);
            int32_t tp2n = ckt.node(tokens[4]);

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
            // O name p1+ p1- p2+ p2- modelname
            if (tokens.size() < 6) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": O element requires name, p1+, p1-, p2+, p2-, modelname");
            }
            DeferredLTRA ol;
            ol.name       = tokens[0];
            ol.p1p        = ckt.node(tokens[1]);
            ol.p1n        = ckt.node(tokens[2]);
            ol.p2p        = ckt.node(tokens[3]);
            ol.p2n        = ckt.node(tokens[4]);
            ol.model_name = tokens[5];
            ol.line_number = line.line_number;
            deferred_ltras.push_back(std::move(ol));

        } else if (elem_type == 's') {
            // S name n+ n- nc+ nc- modelname
            if (tokens.size() < 6) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": S element requires name, n+, n-, nc+, nc-, modelname");
            }
            DeferredVSwitch sd;
            sd.name       = tokens[0];
            sd.np         = ckt.node(tokens[1]);
            sd.nn         = ckt.node(tokens[2]);
            sd.ncp        = ckt.node(tokens[3]);
            sd.ncn        = ckt.node(tokens[4]);
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
            wd.np          = ckt.node(tokens[1]);
            wd.nn          = ckt.node(tokens[2]);
            wd.vsense_name = tokens[3];
            wd.model_name  = tokens[4];
            wd.line_number = line.line_number;
            deferred_cswitches.push_back(std::move(wd));

        } else if (elem_type == 'b') {
            // B name np nn V={expression} or I={expression}
            // Syntax: Bname node+ node- V={expr} | I={expr}
            if (tokens.size() < 4) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": B element requires name, np, nn, V={expr} or I={expr}");
            }
            std::string name = tokens[0];
            int32_t np = ckt.node(tokens[1]);
            int32_t nn = ckt.node(tokens[2]);

            // Join remaining tokens to handle expressions split across tokens
            std::string rest;
            for (size_t i = 3; i < tokens.size(); ++i) {
                if (!rest.empty()) rest += ' ';
                rest += tokens[i];
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

            // Resolve variable references: V() refs use ckt.node(), I() refs deferred
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
                        node_indices[i] = ckt.node(lname);
                    }
                    break;
                }
                case asrc::VarKind::DIFF_VOLTAGE: {
                    std::string ln1 = ref.name1;
                    std::string ln2 = ref.name2;
                    node_indices[i]  = (ln1 == "0" || ln1 == "gnd")
                                       ? GROUND_INTERNAL : ckt.node(ln1);
                    node_indices2[i] = (ln2 == "0" || ln2 == "gnd")
                                       ? GROUND_INTERNAL : ckt.node(ln2);
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
            deferred_asrcs.push_back(std::move(bd));

        }
        // Ignore unknown lines
    }

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

    // Resolve deferred diodes — UCB DIO adapter
    std::unordered_map<std::string, std::unique_ptr<DIOModelCard>> dio_cards;
    for (const auto& dd : deferred_diodes) {
        auto it = models.find(dd.model_name);
        if (it == models.end()) {
            throw ParseError("Line " + std::to_string(dd.line_number) +
                             ": Unknown model '" + dd.model_name + "'");
        }
        auto card_it = dio_cards.find(dd.model_name);
        if (card_it == dio_cards.end()) {
            card_it = dio_cards.emplace(dd.model_name,
                                        to_dio_card(it->second)).first;
        }
        int32_t na = ckt.node(dd.anode);
        int32_t nc = ckt.node(dd.cathode);
        auto dev = DIODevice::make(dd.name, na, nc, dd.geom, *card_it->second);
        if (dd.ic_vd_given) {
            dev->set_ic(dd.ic_vd, dd.ic_vd_given);
        }
        ckt.add_device(std::move(dev));
    }
    for (auto& [name, card] : dio_cards) {
        ckt.add_dio_model_card(std::move(card));
    }

    // Resolve deferred MOSFETs.  Level dispatch:
    //   LEVEL=1           -> MOS1 (Shichman-Hodges)
    //   LEVEL=9           -> MOS9 (Modified Level 3)
    //   LEVEL=8 or 49     -> BSIM3v3
    //   LEVEL=14 (default)-> BSIM4v7
    std::unordered_map<std::string, std::unique_ptr<BSIM4v7ModelCard>> bsim4_cards;
    std::unordered_map<std::string, std::unique_ptr<MOS1ModelCard>> mos1_cards;
    std::unordered_map<std::string, std::unique_ptr<MOS9ModelCard>> mos9_cards;
    std::unordered_map<std::string, std::unique_ptr<BSIM3ModelCard>> bsim3_cards;
    std::unordered_map<std::string, int> mosfet_levels;
    for (const auto& m : deferred_mosfets) {
        auto it = models.find(m.model_name);
        if (it == models.end()) {
            throw ParseError("Line " + std::to_string(m.line_number) +
                             ": Unknown model '" + m.model_name + "'");
        }

        // Detect MOSFET level (cached per model name)
        int level;
        auto lev_it = mosfet_levels.find(m.model_name);
        if (lev_it != mosfet_levels.end()) {
            level = lev_it->second;
        } else {
            level = detect_mosfet_level(it->second);
            mosfet_levels[m.model_name] = level;
        }

        if (level == 1) {
            // MOS1 Level 1 Shichman-Hodges
            auto card_it = mos1_cards.find(m.model_name);
            if (card_it == mos1_cards.end()) {
                try {
                    card_it = mos1_cards.emplace(m.model_name,
                                                  to_mos1_card(it->second)).first;
                } catch (const ParseError& e) {
                    throw ParseError("Line " + std::to_string(m.line_number) +
                                     ": " + e.what());
                }
            }
            MOS1Device::Geom mos1_geom;
            mos1_geom.W   = m.geom.W;
            mos1_geom.L   = m.geom.L;
            mos1_geom.AD  = m.geom.AD;
            mos1_geom.AS  = m.geom.AS;
            mos1_geom.PD  = m.geom.PD;
            mos1_geom.PS  = m.geom.PS;
            mos1_geom.NRD = m.geom.NRD;
            mos1_geom.NRS = m.geom.NRS;
            mos1_geom.M   = m.geom.M;
            auto dev = MOS1Device::make(m.name, m.nd, m.ng, m.ns, m.nb,
                                        mos1_geom, *card_it->second);
            if (m.ic_vds_given || m.ic_vgs_given || m.ic_vbs_given) {
                dev->set_ic(m.ic_vds, m.ic_vds_given,
                            m.ic_vgs, m.ic_vgs_given,
                            m.ic_vbs, m.ic_vbs_given);
            }
            ckt.add_device(std::move(dev));
        } else if (level == 9) {
            // MOS9 Modified Level 3
            auto card_it = mos9_cards.find(m.model_name);
            if (card_it == mos9_cards.end()) {
                try {
                    card_it = mos9_cards.emplace(m.model_name,
                                                  to_mos9_card(it->second)).first;
                } catch (const ParseError& e) {
                    throw ParseError("Line " + std::to_string(m.line_number) +
                                     ": " + e.what());
                }
            }
            MOS9Device::Geom mos9_geom;
            mos9_geom.W   = m.geom.W;
            mos9_geom.L   = m.geom.L;
            mos9_geom.AD  = m.geom.AD;
            mos9_geom.AS  = m.geom.AS;
            mos9_geom.PD  = m.geom.PD;
            mos9_geom.PS  = m.geom.PS;
            mos9_geom.NRD = m.geom.NRD;
            mos9_geom.NRS = m.geom.NRS;
            mos9_geom.M   = m.geom.M;
            auto dev = MOS9Device::make(m.name, m.nd, m.ng, m.ns, m.nb,
                                        mos9_geom, *card_it->second);
            if (m.ic_vds_given || m.ic_vgs_given || m.ic_vbs_given) {
                dev->set_ic(m.ic_vds, m.ic_vds_given,
                            m.ic_vgs, m.ic_vgs_given,
                            m.ic_vbs, m.ic_vbs_given);
            }
            ckt.add_device(std::move(dev));
        } else if (level == 8 || level == 49) {
            // BSIM3v3
            auto card_it = bsim3_cards.find(m.model_name);
            if (card_it == bsim3_cards.end()) {
                try {
                    card_it = bsim3_cards.emplace(m.model_name,
                                                  to_bsim3_card(it->second)).first;
                } catch (const ParseError& e) {
                    throw ParseError("Line " + std::to_string(m.line_number) +
                                     ": " + e.what());
                }
            }
            BSIM3Device::Geom g3;
            g3.W   = m.geom.W;
            g3.L   = m.geom.L;
            g3.AD  = m.geom.AD;
            g3.AS  = m.geom.AS;
            g3.PD  = m.geom.PD;
            g3.PS  = m.geom.PS;
            g3.NRD = m.geom.NRD;
            g3.NRS = m.geom.NRS;
            g3.M   = m.geom.M;
            auto dev = BSIM3Device::make(m.name, m.nd, m.ng, m.ns, m.nb,
                                         g3, *card_it->second);
            if (m.ic_vds_given || m.ic_vgs_given || m.ic_vbs_given) {
                dev->set_ic(m.ic_vds, m.ic_vds_given,
                            m.ic_vgs, m.ic_vgs_given,
                            m.ic_vbs, m.ic_vbs_given);
            }
            ckt.add_device(std::move(dev));
        } else if (level == 14) {
            // BSIM4v7 (LEVEL=14 or default)
            auto card_it = bsim4_cards.find(m.model_name);
            if (card_it == bsim4_cards.end()) {
                try {
                    card_it = bsim4_cards.emplace(m.model_name,
                                                  to_bsim4_card(it->second)).first;
                } catch (const ParseError& e) {
                    throw ParseError("Line " + std::to_string(m.line_number) +
                                     ": " + e.what());
                }
            }
            auto dev = BSIM4v7Device::make(m.name, m.nd, m.ng, m.ns, m.nb,
                                           m.geom, *card_it->second);
            if (m.ic_vds_given || m.ic_vgs_given || m.ic_vbs_given) {
                dev->set_ic(m.ic_vds, m.ic_vds_given,
                            m.ic_vgs, m.ic_vgs_given,
                            m.ic_vbs, m.ic_vbs_given);
            }
            ckt.add_device(std::move(dev));
        } else {
            throw ParseError("Line " + std::to_string(m.line_number) +
                             ": Unsupported MOSFET LEVEL=" +
                             std::to_string(level) + " for model '" +
                             m.model_name + "'");
        }
    }
    // Transfer card ownership to the Circuit (cards must outlive the devices).
    for (auto& [name, card] : bsim4_cards) {
        ckt.add_bsim4_model_card(std::move(card));
    }
    for (auto& [name, card] : mos1_cards) {
        ckt.add_mos1_model_card(std::move(card));
    }
    for (auto& [name, card] : mos9_cards) {
        ckt.add_mos9_model_card(std::move(card));
    }
    for (auto& [name, card] : bsim3_cards) {
        ckt.add_bsim3_model_card(std::move(card));
    }

    // Resolve deferred BJTs (Q-cards dispatch to BJT or VBIC based on LEVEL)
    // LEVEL=1 (default, or unspecified) -> BJT (Gummel-Poon)
    // LEVEL=4 or LEVEL=9               -> VBIC
    std::unordered_map<std::string, std::unique_ptr<BJTModelCard>> bjt_cards;
    std::unordered_map<std::string, std::unique_ptr<VBICModelCard>> vbic_cards;
    for (const auto& q : deferred_bjts) {
        auto it = models.find(q.model_name);
        if (it == models.end()) {
            auto it2 = models.find(to_lower(q.model_name));
            if (it2 == models.end()) {
                throw ParseError("Line " + std::to_string(q.line_number) +
                                 ": Unknown model '" + q.model_name + "'");
            }
            it = it2;
        }
        // Check that the model type is NPN or PNP
        std::string model_type = to_lower(it->second.type);
        if (model_type != "npn" && model_type != "pnp") {
            throw ParseError("Line " + std::to_string(q.line_number) +
                             ": Q card references non-BJT model '" + q.model_name + "'");
        }

        // Determine level: default=1 (BJT), 4 or 9 = VBIC
        auto level_it = it->second.params.find("level");
        int level = (level_it == it->second.params.end()) ? 1
                    : static_cast<int>(level_it->second);

        if (level == 4 || level == 9 || level == 12 || level == 13) {
            // VBIC model
            auto card_it = vbic_cards.find(q.model_name);
            if (card_it == vbic_cards.end()) {
                try {
                    card_it = vbic_cards.emplace(q.model_name,
                                                  to_vbic_card(it->second)).first;
                } catch (const ParseError& e) {
                    throw ParseError("Line " + std::to_string(q.line_number) +
                                     ": " + e.what());
                }
            }
            VBICDevice::Geom vgeom;
            vgeom.area = q.geom.area;
            vgeom.area_given = q.geom.area_given;
            vgeom.m = q.geom.m;
            vgeom.m_given = q.geom.m_given;
            auto dev = VBICDevice::make(q.name, q.nc, q.nb, q.ne, q.ns,
                                        vgeom, *card_it->second);
            if (q.ic_vbe_given || q.ic_vce_given) {
                dev->set_ic(q.ic_vbe, q.ic_vbe_given, q.ic_vce, q.ic_vce_given);
            }
            ckt.add_device(std::move(dev));
        } else {
            // Standard BJT (level 1 or unspecified)
            auto card_it = bjt_cards.find(q.model_name);
            if (card_it == bjt_cards.end()) {
                try {
                    card_it = bjt_cards.emplace(q.model_name,
                                                to_bjt_card(it->second)).first;
                } catch (const ParseError& e) {
                    throw ParseError("Line " + std::to_string(q.line_number) +
                                     ": " + e.what());
                }
            }
            auto dev = BJTDevice::make(q.name, q.nc, q.nb, q.ne, q.ns,
                                       q.geom, *card_it->second);
            if (q.ic_vbe_given || q.ic_vce_given) {
                dev->set_ic(q.ic_vbe, q.ic_vbe_given, q.ic_vce, q.ic_vce_given);
            }
            ckt.add_device(std::move(dev));
        }
    }
    for (auto& [name, card] : bjt_cards) {
        ckt.add_bjt_model_card(std::move(card));
    }
    for (auto& [name, card] : vbic_cards) {
        ckt.add_vbic_model_card(std::move(card));
    }

    // Resolve deferred JFETs
    std::unordered_map<std::string, std::unique_ptr<JFETModelCard>> jfet_cards;
    for (const auto& j : deferred_jfets) {
        auto it = models.find(j.model_name);
        if (it == models.end()) {
            auto it2 = models.find(to_lower(j.model_name));
            if (it2 == models.end()) {
                throw ParseError("Line " + std::to_string(j.line_number) +
                                 ": Unknown model '" + j.model_name + "'");
            }
            it = it2;
        }
        // Check that the model type is NJF or PJF
        std::string model_type = to_lower(it->second.type);
        if (model_type != "njf" && model_type != "pjf") {
            throw ParseError("Line " + std::to_string(j.line_number) +
                             ": J card references non-JFET model '" + j.model_name + "'");
        }
        auto card_it = jfet_cards.find(j.model_name);
        if (card_it == jfet_cards.end()) {
            try {
                card_it = jfet_cards.emplace(j.model_name,
                                              to_jfet_card(it->second)).first;
            } catch (const ParseError& e) {
                throw ParseError("Line " + std::to_string(j.line_number) +
                                 ": " + e.what());
            }
        }
        int32_t nd = ckt.node(j.nd);
        int32_t ng = ckt.node(j.ng);
        int32_t ns = ckt.node(j.ns);
        auto dev = JFETDevice::make(j.name, nd, ng, ns,
                                     j.geom, *card_it->second);
        if (j.ic_vds_given || j.ic_vgs_given) {
            dev->set_ic(j.ic_vds, j.ic_vds_given, j.ic_vgs, j.ic_vgs_given);
        }
        ckt.add_device(std::move(dev));
    }
    for (auto& [name, card] : jfet_cards) {
        ckt.add_jfet_model_card(std::move(card));
    }

    // Resolve deferred HFET2s (Z elements with nhfet/phfet level=6)
    std::unordered_map<std::string, std::unique_ptr<HFET2ModelCard>> hfet2_cards;
    for (const auto& z : deferred_hfet2s) {
        auto it = models.find(z.model_name);
        if (it == models.end()) {
            auto it2 = models.find(to_lower(z.model_name));
            if (it2 == models.end()) {
                throw ParseError("Line " + std::to_string(z.line_number) +
                                 ": Unknown model '" + z.model_name + "'");
            }
            it = it2;
        }
        // Check that the model type is nhfet or phfet
        std::string model_type = to_lower(it->second.type);
        if (model_type != "nhfet" && model_type != "phfet") {
            throw ParseError("Line " + std::to_string(z.line_number) +
                             ": Z card references non-HFET model '" + z.model_name + "'");
        }
        auto card_it = hfet2_cards.find(z.model_name);
        if (card_it == hfet2_cards.end()) {
            try {
                card_it = hfet2_cards.emplace(z.model_name,
                                              to_hfet2_card(it->second)).first;
            } catch (const ParseError& e) {
                throw ParseError("Line " + std::to_string(z.line_number) +
                                 ": " + e.what());
            }
        }
        int32_t nd = ckt.node(z.nd);
        int32_t ng = ckt.node(z.ng);
        int32_t ns = ckt.node(z.ns);
        auto dev = HFET2Device::make(z.name, nd, ng, ns,
                                     z.geom, *card_it->second);
        if (z.ic_vds_given || z.ic_vgs_given) {
            dev->set_ic(z.ic_vds, z.ic_vds_given, z.ic_vgs, z.ic_vgs_given);
        }
        ckt.add_device(std::move(dev));
    }
    for (auto& [name, card] : hfet2_cards) {
        ckt.add_hfet2_model_card(std::move(card));
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

        ckt.add_device(std::make_unique<LossyTransmissionLine>(
            ol.name, ol.p1p, ol.p1n, ol.p2p, ol.p2n, lmodel));
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

        ckt.add_device(std::make_unique<ASRCDevice>(
            bd.name, bd.np, bd.nn, bd.mode,
            std::move(bd.expr),
            std::move(bd.node_indices),
            std::move(bd.node_indices2),
            std::move(vsource_ptrs)));
    }

    ckt.finalize();
    return ckt;
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

} // namespace neospice
