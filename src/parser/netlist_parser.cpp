#include "parser/netlist_parser.hpp"
#include "parser/tokenizer.hpp"
#include "parser/subcircuit.hpp"
#include "parser/subcircuit_expand.hpp"
#include "parser/expression.hpp"
#include "parser/model_cards.hpp"
#include "devices/resistor.hpp"
#include "devices/capacitor.hpp"
#include "devices/inductor.hpp"
#include "devices/coupled_inductor.hpp"
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include "devices/diode.hpp"
#include "devices/vcvs.hpp"
#include "devices/vccs.hpp"
#include "devices/ccvs.hpp"
#include "devices/cccs.hpp"
#include "devices/bsim4v7/bsim4v7_device.hpp"
#include "devices/bjt/bjt_device.hpp"
#include "devices/jfet/jfet_device.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_map>

namespace neospice {

namespace {

std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
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
    auto lines = tokenize(netlist.substr(start));

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

    // Pass 0.25: Pre-collect top-level .param entries so that expansion
    // (Pass 0.5) can resolve parameter expressions like R={myR}.
    std::unordered_map<std::string, double> global_params;
    {
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

    // Pass 0.5: Expand X instances into primitive element lines.
    // After this step, `lines` contains no X elements.
    // Always call — even when subcircuit_defs_ is empty — so that X lines
    // referencing undefined subcircuits produce a proper ParseError.
    lines = expand_all_instances(lines, subcircuit_defs_, global_params);

    // Storage for params and models
    std::unordered_map<std::string, double> params;
    std::unordered_map<std::string, ModelCard> models;

    // Deferred diodes: (name, anode_name, cathode_name, model_name, line_number)
    struct DeferredDiode {
        std::string name;
        std::string anode;
        std::string cathode;
        std::string model_name;
        int line_number;
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
        int line_number;
    };
    std::vector<DeferredCCCS> deferred_cccs;

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

    // Pass 1: collect .model and .param cards
    std::vector<std::pair<std::string, std::string>> raw_params;
    for (const auto& line : lines) {
        if (line.tokens.empty()) continue;
        std::string first = to_lower(line.tokens[0]);

        if (first == ".model") {
            auto card = parse_model_card(line.tokens);
            models[card.name] = card;
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
                    double val = parse_spice_number(tokens[i].substr(eq_pos + 1));
                    if (key == "reltol") ckt.options.reltol = val;
                    else if (key == "abstol") ckt.options.abstol = val;
                    else if (key == "vntol") ckt.options.vntol = val;
                    else if (key == "gmin") ckt.options.gmin = val;
                    else if (key == "trtol") ckt.options.trtol = val;
                    else if (key == "temp") ckt.options.temp = val + 273.15;
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
                    if (!sig.empty())
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
            }
            // Skip .model, .param (already handled), .print, .include, .lib, .endl, etc.
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
            // R name n+ n- value
            if (tokens.size() < 4) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": Resistor requires name, n+, n-, value");
            }
            std::string name = tokens[0];
            int32_t np = ckt.node(tokens[1]);
            int32_t nn = ckt.node(tokens[2]);
            double val = parse_spice_number(tokens[3]);
            ckt.add_device(std::make_unique<Resistor>(name, np, nn, val));

        } else if (elem_type == 'c') {
            if (tokens.size() < 4) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": Capacitor requires name, n+, n-, value");
            }
            std::string name = tokens[0];
            int32_t np = ckt.node(tokens[1]);
            int32_t nn = ckt.node(tokens[2]);
            double val = parse_spice_number(tokens[3]);
            ckt.add_device(std::make_unique<Capacitor>(name, np, nn, val));

        } else if (elem_type == 'l') {
            if (tokens.size() < 4) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": Inductor requires name, n+, n-, value");
            }
            std::string name = tokens[0];
            int32_t np = ckt.node(tokens[1]);
            int32_t nn = ckt.node(tokens[2]);
            double val = parse_spice_number(tokens[3]);
            ckt.add_device(std::make_unique<Inductor>(name, np, nn, val));

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
            ckt.add_device(std::move(is));

        } else if (elem_type == 'd') {
            // D name anode cathode modelname
            if (tokens.size() < 4) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": Diode requires name, anode, cathode, modelname");
            }
            deferred_diodes.push_back({tokens[0], tokens[1], tokens[2], tokens[3],
                                       line.line_number});

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
                // Silently ignore unknown M-card keys — UCB supports ~20 more
                // (e.g., M=multiplier, TEMP, DTEMP, RBDB, RBSB, ...) that
                // default cleanly in BSIM4setup.
            }
            deferred_mosfets.push_back(std::move(m));

        } else if (elem_type == 'e') {
            // E name np nn nc+ nc- gain
            if (tokens.size() < 6) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": VCVS requires name, np, nn, nc+, nc-, gain");
            }
            std::string name = tokens[0];
            int32_t np  = ckt.node(tokens[1]);
            int32_t nn  = ckt.node(tokens[2]);
            int32_t ncp = ckt.node(tokens[3]);
            int32_t ncn = ckt.node(tokens[4]);
            double  gain = parse_spice_number(tokens[5]);
            ckt.add_device(std::make_unique<VCVS>(name, np, nn, ncp, ncn, gain));

        } else if (elem_type == 'g') {
            // G name np nn nc+ nc- gm
            if (tokens.size() < 6) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": VCCS requires name, np, nn, nc+, nc-, gm");
            }
            std::string name = tokens[0];
            int32_t np  = ckt.node(tokens[1]);
            int32_t nn  = ckt.node(tokens[2]);
            int32_t ncp = ckt.node(tokens[3]);
            int32_t ncn = ckt.node(tokens[4]);
            double  gm  = parse_spice_number(tokens[5]);
            ckt.add_device(std::make_unique<VCCS>(name, np, nn, ncp, ncn, gm));

        } else if (elem_type == 'h') {
            // H name np nn Vsense transresistance
            if (tokens.size() < 5) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": CCVS requires name, np, nn, Vsense, transresistance");
            }
            DeferredCCVS hd;
            hd.name        = tokens[0];
            hd.np          = ckt.node(tokens[1]);
            hd.nn          = ckt.node(tokens[2]);
            hd.vsense_name = tokens[3];
            hd.rm          = parse_spice_number(tokens[4]);
            hd.line_number = line.line_number;
            deferred_ccvs.push_back(std::move(hd));

        } else if (elem_type == 'f') {
            // F name np nn Vsense gain
            if (tokens.size() < 5) {
                throw ParseError("Line " + std::to_string(line.line_number) +
                                 ": CCCS requires name, np, nn, Vsense, gain");
            }
            DeferredCCCS fd;
            fd.name        = tokens[0];
            fd.np          = ckt.node(tokens[1]);
            fd.nn          = ckt.node(tokens[2]);
            fd.vsense_name = tokens[3];
            fd.gain        = parse_spice_number(tokens[4]);
            fd.line_number = line.line_number;
            deferred_cccs.push_back(std::move(fd));

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

        } else if (elem_type == 'b') {
            throw ParseError("Line " + std::to_string(line.line_number) +
                             ": Unsupported element type '" + std::string(1, elem_type) + "'");
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
        ckt.add_device(std::make_unique<CCCS>(fd.name, fd.np, fd.nn, fd.gain, vs));
    }

    // Resolve deferred diodes
    for (const auto& dd : deferred_diodes) {
        auto it = models.find(dd.model_name);
        if (it == models.end()) {
            throw ParseError("Line " + std::to_string(dd.line_number) +
                             ": Unknown model '" + dd.model_name + "'");
        }
        DiodeModel dm = to_diode_model(it->second);
        int32_t na = ckt.node(dd.anode);
        int32_t nc = ckt.node(dd.cathode);
        ckt.add_device(std::make_unique<Diode>(dd.name, na, nc, dm));
    }

    // Resolve deferred MOSFETs.  A single BSIM4v7ModelCard is created per
    // distinct .model name (N:1 instance→card) and retained so all instances
    // that reference the same name share UCB's BSIM4instances linked list.
    // Ownership is transferred to the Circuit after all devices are made,
    // guaranteeing the cards outlive the BSIM4v7Device non-owning back-pointers.
    std::unordered_map<std::string, std::unique_ptr<BSIM4v7ModelCard>> bsim4_cards;
    for (const auto& m : deferred_mosfets) {
        auto it = models.find(m.model_name);
        if (it == models.end()) {
            throw ParseError("Line " + std::to_string(m.line_number) +
                             ": Unknown model '" + m.model_name + "'");
        }
        // Lazy-create BSIM4v7ModelCard — to_bsim4_card validates LEVEL=14
        // and NMOS/PMOS type, throws ParseError otherwise.
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
        // Must happen before finalize() — setup() clears ic fields when !Given.
        if (m.ic_vds_given || m.ic_vgs_given || m.ic_vbs_given) {
            dev->set_ic(m.ic_vds, m.ic_vds_given,
                        m.ic_vgs, m.ic_vgs_given,
                        m.ic_vbs, m.ic_vbs_given);
        }
        ckt.add_device(std::move(dev));
    }
    // Transfer card ownership to the Circuit (cards must outlive the devices).
    for (auto& [name, card] : bsim4_cards) {
        ckt.add_bsim4_model_card(std::move(card));
    }

    // Resolve deferred BJTs
    std::unordered_map<std::string, std::unique_ptr<BJTModelCard>> bjt_cards;
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
    for (auto& [name, card] : bjt_cards) {
        ckt.add_bjt_model_card(std::move(card));
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
