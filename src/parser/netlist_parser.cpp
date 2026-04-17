#include "parser/netlist_parser.hpp"
#include "parser/tokenizer.hpp"
#include "parser/expression.hpp"
#include "parser/model_cards.hpp"
#include "devices/resistor.hpp"
#include "devices/capacitor.hpp"
#include "devices/inductor.hpp"
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include "devices/diode.hpp"
#include "devices/bsim4v7/bsim4v7_device.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
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
    size_t orig_idx = idx;
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

    // Deferred MOSFETs: parsed M-cards are resolved in a second pass once
    // all .model cards are known.  node indices are already mapped (we have
    // access to `ckt` when scanning element lines).
    struct DeferredMosfet {
        std::string name;
        int32_t nd, ng, ns, nb;
        std::string model_name;
        BSIM4v7Device::Geom geom;
        int line_number;
    };
    std::vector<DeferredMosfet> deferred_mosfets;

    // Pass 1: collect .model and .param cards
    for (const auto& line : lines) {
        if (line.tokens.empty()) continue;
        std::string first = to_lower(line.tokens[0]);

        if (first == ".model") {
            auto card = parse_model_card(line.tokens);
            models[card.name] = card;
        } else if (first == ".param") {
            // .param key=value ...
            for (size_t i = 1; i < line.tokens.size(); ++i) {
                auto eq_pos = line.tokens[i].find('=');
                if (eq_pos != std::string::npos) {
                    std::string key = line.tokens[i].substr(0, eq_pos);
                    std::string val_str = line.tokens[i].substr(eq_pos + 1);
                    params[key] = eval_expression(val_str, params);
                }
            }
        }
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
            }
            // Skip .model, .param (already handled), .save, .print, .include, .lib, .endl, etc.
            continue;
        }

        // Element lines — dispatch by first character
        char elem_type = std::tolower(static_cast<unsigned char>(first[0]));

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

        } else if (elem_type == 'e' || elem_type == 'f' || elem_type == 'g' ||
                   elem_type == 'h' || elem_type == 'b' || elem_type == 'x') {
            throw ParseError("Line " + std::to_string(line.line_number) +
                             ": Unsupported element type '" + std::string(1, elem_type) + "'");
        }
        // Ignore unknown lines
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
        ckt.add_device(BSIM4v7Device::make(m.name, m.nd, m.ng, m.ns, m.nb,
                                           m.geom, *card_it->second));
    }
    // Transfer card ownership to the Circuit (cards must outlive the devices).
    for (auto& [name, card] : bsim4_cards) {
        ckt.add_bsim4_model_card(std::move(card));
    }

    ckt.finalize();
    return ckt;
}

Circuit NetlistParser::parse_file(const std::string& filepath) {
    std::ifstream ifs(filepath);
    if (!ifs.is_open()) {
        throw ParseError("Cannot open file: " + filepath);
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return parse(oss.str());
}

} // namespace neospice
