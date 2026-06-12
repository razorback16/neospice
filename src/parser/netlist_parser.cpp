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
#include <cstdio>
#include <cstring>
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

// Scanner that yields "node atoms" from a whitespace-split token stream while
// following ngspice gettok_node semantics: '(', ')', ',' and whitespace ALL act
// as delimiters. This makes every controlled-source node-list form parse
// identically — e.g. "(np,nn)", "(np nn)", "np nn", two separate parenthesized
// groups "(out+,out-) (nc+,nc-)", and even "POLY(1),(2,3)" where control nodes
// are glued onto the POLY token after its count.
//
// `tok_index` is where scanning begins; `char_offset` is an offset into that
// first token (used to resume scanning past "POLY(n)" inside the same token).
// After construction, `next()` returns successive atoms and advances state.
class NodeAtomScanner {
public:
    NodeAtomScanner(const std::vector<std::string>& tokens, size_t tok_index,
                    size_t char_offset = 0)
        : tokens_(tokens), idx_(tok_index), off_(char_offset) {}

    // Fetch the next node atom. Returns false when the token stream is
    // exhausted with no further atom.
    bool next(std::string& out) {
        out.clear();
        while (idx_ < tokens_.size()) {
            const std::string& tok = tokens_[idx_];
            while (off_ < tok.size()) {
                char c = tok[off_];
                if (c == '(' || c == ')' || c == ',') {
                    ++off_;
                    if (!out.empty()) return true;
                    continue;  // skip leading delimiters
                }
                out += c;
                ++off_;
            }
            // End of this token == whitespace delimiter; advance to next token.
            ++idx_;
            off_ = 0;
            if (!out.empty()) return true;
        }
        return !out.empty();
    }

    // Index of the next token not yet (fully) consumed. After reading the node
    // list, scanning of remaining numeric data (coeffs/gain) resumes here.
    // If we are mid-token, point at the next whole token so the consumer (which
    // works on whole tokens) does not re-read partially-consumed node data.
    size_t resume_token_index() const {
        // Once any char of the current token has been consumed (off_ > 0), that
        // token's content has been read as node atoms — whether consumption
        // stopped mid-token or exactly at its end (e.g. the trailing ')' of
        // "(3,4)"). Either way, whole-token resumption must skip it. When
        // off_ == 0 the scanner advanced cleanly to a fresh token, so idx_
        // already points at the next unread token.
        if (idx_ < tokens_.size() && off_ > 0)
            return idx_ + 1;
        return idx_;
    }

private:
    const std::vector<std::string>& tokens_;
    size_t idx_;
    size_t off_;
};

// Parse the PWL point list of an E/G TABLE form. ngspice's gettok_node treats
// '(', ')', ',', and whitespace all as delimiters, so the parenthesized comma
// form "(x,y) (x,y)", the parenthesized space form "(x y) (x y)", and the bare
// newline-separated form "x y\n x y" all tokenize identically. We collect the
// numeric tokens and pair them up. Non-numeric tokens are skipped (table data
// is purely numeric once the control expression has been separated out).
std::vector<TablePoint> parse_table_points(const std::string& joined) {
    std::vector<TablePoint> pts;
    std::vector<double> nums;
    std::string cur;
    auto flush = [&]() {
        if (!cur.empty()) {
            try { nums.push_back(parse_spice_number(cur)); } catch (...) {}
            cur.clear();
        }
    };
    for (char ch : joined) {
        if (ch == '(' || ch == ')' || ch == ',' ||
            std::isspace(static_cast<unsigned char>(ch)))
            flush();
        else
            cur += ch;
    }
    flush();
    for (size_t i = 0; i + 1 < nums.size(); i += 2)
        pts.push_back({nums[i], nums[i + 1]});
    return pts;
}

// Case-insensitive equality of a token against a lowercase literal, without
// allocating a lowercased copy. Used in the hot per-line directive dispatch
// loops (pass025 / pass04 / pass1) which would otherwise lowercase tokens[0]
// of every line in a large included library.
inline bool iequals(const std::string& tok, const char* lit) {
    size_t i = 0;
    for (; i < tok.size(); ++i) {
        char c = tok[i];
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if (lit[i] == '\0' || c != lit[i]) return false;
    }
    return lit[i] == '\0';
}

// Accept both `.param` and `.params` (and trailing plural) — ngspice matches
// the directive via ciprefix, so `.params` (source.lib) is treated as `.param`.
inline bool is_param_card(const std::string& tok) {
    return iequals(tok, ".param") || iequals(tok, ".params");
}

// Parse a .func directive from a tokenized line and add to func_defs.
// .func name(arg1, arg2, ...) {body}
// The tokenizer splits on whitespace, so tokens will be like:
//   [".func", "name(arg1,arg2,...)", "{body}"] or with spaces in body
//   [".func", "name(arg1,", "arg2,...)", "{body}"]
// (definition moved below the anonymous namespace so it has external linkage
//  and can be reused from subcircuit_expand.cpp — see parse_func_def near the
//  end of the anonymous namespace block.)

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
        // No parentheses — parse remaining tokens as bare values
        // (e.g. PULSE -0.68 0.02 148NS 0.33US 0.33US 6US 6.66US)
        // Skip the keyword token (PULSE/SIN/etc.) that idx currently points to
        ++idx;
        std::vector<double> values;
        for (; idx < tokens.size(); ++idx) {
            std::string t = tokens[idx];
            // Lowercase for keyword detection
            std::string tl = t;
            std::transform(tl.begin(), tl.end(), tl.begin(), ::tolower);
            if (tl == "dc" || tl == "ac" || tl == "pulse" || tl == "sin" ||
                tl == "pwl" || tl == "exp" || tl == "sffm" || tl == "am")
                break;
            try { values.push_back(parse_spice_number(t)); }
            catch (...) { break; }
        }
        return values;
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
        try { values.push_back(parse_spice_number(tok)); }
        catch (...) { values.push_back(0); }
    }
    return values;
}

// Parse a source line (VSource or ISource) for DC, AC, PULSE, SIN keywords
struct ParsedSourceSpec {
    double dc_val = 0.0;
    bool   dc_given = false;  // ngspice VSRCdcGiven: explicit "DC x" or bare leading value
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

// Parse POLY coefficients from tokens starting at idx.
// Handles both space-separated values and PSpice "(val,val,...)" form.
static void parse_poly_coeffs(const std::vector<std::string>& tokens, size_t& idx,
                               std::vector<double>& coeffs) {
    for (; idx < tokens.size(); ++idx) {
        const auto& tok = tokens[idx];
        if (tok.empty()) continue;
        if (tok.front() == '(' || (tok.size() > 1 && tok.front() == '(' && tok.back() == ')')) {
            // Parenthesized comma-separated coefficients: (v1,v2,v3)
            std::string inner = tok;
            // Strip leading '(' and trailing ')'
            if (inner.front() == '(') inner = inner.substr(1);
            if (!inner.empty() && inner.back() == ')') inner.pop_back();
            // Split by comma
            std::istringstream ss(inner);
            std::string part;
            while (std::getline(ss, part, ',')) {
                if (part.empty()) continue;
                try { coeffs.push_back(parse_spice_number(part)); } catch (...) {}
            }
        } else {
            try { coeffs.push_back(parse_spice_number(tok)); } catch (...) { break; }
        }
    }
}

ParsedSourceSpec parse_source_spec(const std::vector<std::string>& tokens, size_t start_idx) {
    ParsedSourceSpec spec;
    size_t i = start_idx;

    while (i < tokens.size()) {
        std::string lower = to_lower(tokens[i]);

        if (lower == "dc") {
            ++i;
            if (i < tokens.size()) {
                try { spec.dc_val = parse_spice_number(tokens[i]); }
                catch (...) { spec.dc_val = 0; }
                spec.dc_given = true;
                ++i;
            }
        } else if (lower == "ac") {
            ++i;
            if (i < tokens.size()) {
                try { spec.ac_mag = parse_spice_number(tokens[i]); }
                catch (...) { spec.ac_mag = 0; }
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
            // Try to parse as a bare DC value (no "DC" keyword).
            // ngspice treats a bare leading numeric value as VSRCdcGiven=TRUE.
            try {
                spec.dc_val = parse_spice_number(tokens[i]);
                spec.dc_given = true;
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

// Parse a .func directive from a tokenized line and add to func_defs.
// .func name(arg1, arg2, ...) {body}
// The tokenizer splits on whitespace, so tokens will be like:
//   [".func", "name(arg1,arg2,...)", "{body}"] or with spaces in body
//   [".func", "name(arg1,", "arg2,...)", "{body}"]
// Declared in expression.hpp; external linkage so subcircuit_expand.cpp can
// reuse it to inline subckt-local funcs.
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

    // Surface PSpice/LTspice compatibility mode to the device layer. ngspice
    // applies several compat-only tweaks under -D ngbehavior=ps/lt/psa (e.g. the
    // diode RS=0 virtual series conductance in diosetup.c). We have no global
    // ngbehavior flag; the closest faithful proxy is the auto-detected dialect.
    ckt.options.pspice_compat = (dialect_ == SpiceDialect::PSPICE);

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

            // Only lines beginning with '.' can be .subckt/.ends; for the bulk
            // of library lines (.model cards, elements) skip the lowercasing.
            const std::string& tok0 = line.tokens[0];
            bool is_subckt = false, is_ends = false;
            if (tok0[0] == '.') {
                is_subckt = iequals(tok0, ".subckt");
                // .ends matches ".ends" exactly or any token starting ".ends..."
                if (!is_subckt && tok0.size() >= 5 &&
                    (tok0[1]=='e'||tok0[1]=='E') && (tok0[2]=='n'||tok0[2]=='N') &&
                    (tok0[3]=='d'||tok0[3]=='D') && (tok0[4]=='s'||tok0[4]=='S'))
                    is_ends = true;
            }

            if (is_subckt) {
                if (line.tokens.size() < 2) {
                    fprintf(stderr, "Warning: Line %d: .subckt requires a subcircuit name — skipping\n", line.line_number);
                    continue;
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
                    std::string tok = line.tokens[i];
                    // Commas separate parameters in PSpice subckt headers
                    // ("Params:B0=1, B1=1,"): drop a standalone comma and strip
                    // a trailing comma glued to a token.
                    if (tok == ",") continue;
                    if (!tok.empty() && tok.back() == ',') tok.pop_back();
                    std::string tok_lower = to_lower(tok);
                    // A PSpice section keyword may be glued to the first param
                    // ("Params:B0=1") — strip the prefix so the rest parses.
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
                        // key=value => parameter default
                        std::string key = to_lower(tok.substr(0, eq_pos));
                        std::string val = tok.substr(eq_pos + 1);
                        // Handle "key= value" (space after =)
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
                        // Handle "key = value" (spaces around =)
                        std::string key = to_lower(tok);
                        std::string val = line.tokens[i + 2];
                        current_def.default_params.emplace_back(key, val);
                        i += 2;
                    } else {
                        // No '=' => port name (some dialects mix ports and params freely)
                        current_def.ports.push_back(to_lower(tok));
                    }
                }

                depth++;

            } else if (is_ends) {
                if (depth == 0) {
                    // Stray .ends without matching .subckt — silently skip
                    continue;
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

        while (depth > 0) {
            fprintf(stderr, "Warning: implicitly closing unterminated .subckt '%s'\n",
                    current_def.name.c_str());
            if (depth == 1) {
                state.subcircuit_defs[current_def.name] = std::move(current_def);
                current_def = SubcircuitDef{};
            } else if (!def_stack.empty()) {
                current_def = std::move(def_stack.back());
                def_stack.pop_back();
            }
            depth--;
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
            if (iequals(line.tokens[0], ".func")) {
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
            if (is_param_card(line.tokens[0])) {
                for (size_t i = 1; i < line.tokens.size(); ++i) {
                    auto eq_pos = line.tokens[i].find('=');
                    if (eq_pos != std::string::npos) {
                        std::string key = to_lower(line.tokens[i].substr(0, eq_pos));
                        std::string val_str = line.tokens[i].substr(eq_pos + 1);
                        if (val_str.empty() && i + 1 < line.tokens.size()) {
                            val_str = line.tokens[++i];
                        }
                        if (!key.empty()) {
                            pre_raw_params.emplace_back(key, val_str);
                        }
                    } else if (line.tokens[i] != "=" &&
                               i + 1 < line.tokens.size() && line.tokens[i + 1] == "=" &&
                               i + 2 < line.tokens.size()) {
                        std::string key = to_lower(line.tokens[i]);
                        std::string val_str = line.tokens[i + 2];
                        pre_raw_params.emplace_back(key, val_str);
                        i += 2;
                    } else if (line.tokens[i] != "=" &&
                               i + 1 < line.tokens.size() &&
                               line.tokens[i + 1].size() > 1 &&
                               line.tokens[i + 1].front() == '=') {
                        // Glued form: "key", "=val"  (e.g. ".param Vpp =525mV")
                        std::string key = to_lower(line.tokens[i]);
                        std::string val_str = line.tokens[i + 1].substr(1);
                        pre_raw_params.emplace_back(key, val_str);
                        i += 1;
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
        if (iequals(line.tokens[0], ".global")) {
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
        if (iequals(line.tokens[0], ".func")) {
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
        // Only .model / .param are handled here; both start with '.'.
        if (line.tokens[0][0] != '.') continue;

        if (iequals(line.tokens[0], ".model")) {
            // Lazy: store only the raw token vector keyed by lowercased name.
            // The expensive parse_model_card + AKO resolution is deferred to
            // ensure_model(), triggered on first reference by an instance.
            // source_order is assigned here, in card order, so that setup
            // ordering is identical to the old eager path regardless of the
            // order in which models are later materialized.
            if (line.tokens.size() >= 2) {
                std::string key = to_lower(line.tokens[1]);
                ParseState::RawModel rm;
                rm.tokens = line.tokens;
                rm.source_order = state.next_model_order++;
                state.model_raw[key] = std::move(rm);
            }
        } else if (is_param_card(line.tokens[0])) {
            // .param key=value  or  .param key={expr}  or  .param key = value
            // Collect raw (name, expression) pairs; resolve later in dependency order.
            for (size_t i = 1; i < line.tokens.size(); ++i) {
                auto eq_pos = line.tokens[i].find('=');
                if (eq_pos != std::string::npos) {
                    std::string key = line.tokens[i].substr(0, eq_pos);
                    std::string val_str = line.tokens[i].substr(eq_pos + 1);
                    if (val_str.empty() && i + 1 < line.tokens.size()) {
                        val_str = line.tokens[++i];
                    }
                    if (!key.empty()) {
                        raw_params.emplace_back(key, val_str);
                    }
                } else if (line.tokens[i] != "=" &&
                           i + 1 < line.tokens.size() && line.tokens[i + 1] == "=" &&
                           i + 2 < line.tokens.size()) {
                    std::string key = line.tokens[i];
                    std::string val_str = line.tokens[i + 2];
                    raw_params.emplace_back(key, val_str);
                    i += 2;
                }
            }
        }
    }
    // NOTE: PSpice AKO (A Kind Of) inheritance and typed-map population are no
    // longer resolved eagerly here. They happen on demand in ensure_model(),
    // which is invoked the first time a model is referenced by an instance.

    // Resolve all .param definitions in dependency order (handles forward references)
    if (!raw_params.empty()) {
        state.params = resolve_params(raw_params);
    }
}

// ---------------------------------------------------------------------------
// ensure_model — lazily parse + AKO-resolve + memoize a single model card.
//
// Replaces the old eager parse-everything path: parse_model_card and AKO merge
// only run for models that an instance actually references. The result is cached
// in state.models (and the typed res/cap/ind maps) so subsequent lookups are
// O(1). AKO bases are materialized transitively. Behavior (warnings, merge
// semantics, source ordering, lowercase normalization) matches the old code for
// any model that is actually used.
// ---------------------------------------------------------------------------
ModelCard* NetlistParser::ensure_model(ParseState& state, const std::string& name) {
    std::string key = to_lower(name);

    auto raw_it = state.model_raw.find(key);
    if (raw_it == state.model_raw.end()) {
        // Not in the raw index. It may still be a pre-populated cache entry
        // (the expand_subcircuit_into / API path seeds state.models directly).
        auto cached = state.models.find(name);
        if (cached != state.models.end()) return &cached->second;
        return nullptr; // no such .model
    }

    ParseState::RawModel& raw = raw_it->second;

    // Already memoized?
    if (raw.parsed) {
        auto cached = state.models.find(raw.cache_key);
        if (cached != state.models.end()) return &cached->second;
    }

    // Cycle guard: if we re-enter for a model currently being resolved, this is
    // circular AKO inheritance. Match the old "circular inheritance detected"
    // behavior: warn and treat as if the AKO link were absent.
    if (raw.resolving) {
        fprintf(stderr, "Warning: .model %s AKO: circular inheritance detected — ignoring\n",
                name.c_str());
        return nullptr;
    }
    raw.resolving = true;

    ModelCard card = parse_model_card(raw.tokens);
    card.source_order = raw.source_order;

    // Resolve AKO inheritance on demand.
    if (!card.ako_base.empty()) {
        // Normalize ako_base to lowercase so it matches the lowercased model
        // keys produced by subcircuit expansion (e.g. "QON" -> "qon").
        std::transform(card.ako_base.begin(), card.ako_base.end(),
                       card.ako_base.begin(), ::tolower);

        std::string base_key = card.ako_base;
        bool base_exists = state.model_raw.count(base_key) != 0;
        if (!base_exists) {
            // Try instance-prefixed variant: if this model is "x1.qp" and
            // ako_base is "qon", try "x1.qon".
            auto dot_pos = key.rfind('.');
            if (dot_pos != std::string::npos) {
                std::string prefixed = key.substr(0, dot_pos + 1) + card.ako_base;
                if (state.model_raw.count(prefixed) != 0) {
                    base_key = prefixed;
                    card.ako_base = prefixed;
                    base_exists = true;
                }
            }
        }

        if (!base_exists) {
            fprintf(stderr, "Warning: .model %s AKO: base model '%s' not found — skipping inheritance\n",
                    name.c_str(), card.ako_base.c_str());
            card.ako_base.clear();
        } else {
            ModelCard* base = ensure_model(state, base_key);
            if (base == nullptr) {
                // base resolution failed (e.g. circular) — drop the link.
                card.ako_base.clear();
            } else {
                // Inherit type if derived doesn't specify one.
                if (card.type.empty()) {
                    card.type = base->type;
                }
                // Merge params: start with base, overlay derived.
                auto merged = base->params;
                for (const auto& [pk, pv] : card.params) {
                    merged[pk] = pv;
                }
                card.params = std::move(merged);
                card.ako_base.clear();
            }
        }
    }

    raw.resolving = false;
    raw.parsed = true;
    raw.cache_key = card.name;

    // Memoize. The cache is keyed by card.name (original case), exactly as the
    // old eager path keyed state.models. Some device resolve handlers (e.g.
    // MOSFET) look up by the original-case name only, so this preserves their
    // behavior. The raw index (model_raw) remains lowercase-keyed.
    std::string cache_key = card.name;
    auto [ins_it, _] = state.models.emplace(cache_key, std::move(card));
    ModelCard& stored = ins_it->second;

    // Populate typed model maps for r/c/l on first materialization. These are
    // lowercase-keyed (matching every res/cap/ind lookup site).
    if (stored.type == "r") {
        state.res_models[key] = to_resistor_model(stored);
    } else if (stored.type == "c") {
        state.cap_models[key] = to_capacitor_model(stored);
    } else if (stored.type == "l") {
        state.ind_models[key] = to_inductor_model(stored);
    }

    return &stored;
}

// ---------------------------------------------------------------------------
// materialize_all_models — eagerly parse every collected .model card. Used by
// the load_definitions() API path which must hand back a fully-populated map.
// ---------------------------------------------------------------------------
void NetlistParser::materialize_all_models(ParseState& state) {
    // Iterate over a snapshot of keys: ensure_model may insert into state.models
    // but does not modify model_raw's key set.
    std::vector<std::string> keys;
    keys.reserve(state.model_raw.size());
    for (const auto& [k, _] : state.model_raw) keys.push_back(k);
    for (const auto& k : keys) ensure_model(state, k);
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
    auto& deferred_table_vccs = state.deferred_table_vccs;

    using DeferredCCVS = ParseState::DeferredCCVS;
    using DeferredCCCS = ParseState::DeferredCCCS;
    using DeferredPolyCCVS = ParseState::DeferredPolyCCVS;
    using DeferredPolyCCCS = ParseState::DeferredPolyCCCS;
    using DeferredCoupledInductor = ParseState::DeferredCoupledInductor;
    using DeferredVSwitch = ParseState::DeferredVSwitch;
    using DeferredCSwitch = ParseState::DeferredCSwitch;
    using DeferredLTRA = ParseState::DeferredLTRA;
    using DeferredASRC = ParseState::DeferredASRC;

    // Helper: parse a component value that may be a plain number, {expr}, or 'expr'
    auto parse_value = [&](const std::string& token) -> double {
        if (token.size() >= 2 && token.front() == '{' && token.back() == '}') {
            return eval_expression(token, state.params);
        }
        if (token.size() >= 2 && token.front() == '\'' && token.back() == '\'') {
            std::string expr = token.substr(1, token.size() - 2);
            return eval_expression(expr, state.params);
        }
        return parse_spice_number(token);
    };

    // Lazy-model pre-pass: materialize every .model card that is actually
    // referenced by an element line before we start parsing elements. This
    // guarantees that membership tests against state.models / the typed
    // res/cap/ind maps (e.g. "is this token a model name or a node?") and all
    // later content lookups in pass 2/3 behave exactly as they did when every
    // model was parsed eagerly — but only for models that are used.
    //
    // Any token on a non-dot element line that matches a collected .model name
    // is materialized (which also pulls in its AKO base transitively). Tokens
    // that don't name a model are ignored. This is O(used models), not
    // O(models-in-library).
    if (!state.model_raw.empty()) {
        for (const auto& line : state.lines) {
            if (line.tokens.empty()) continue;
            if (!line.tokens[0].empty() && line.tokens[0][0] == '.') continue;
            for (const auto& tok : line.tokens) {
                auto eq = tok.find('=');
                std::string cand = (eq == std::string::npos) ? tok : tok.substr(0, eq);
                std::string lc = to_lower(cand);
                auto rit = state.model_raw.find(lc);
                if (rit != state.model_raw.end() && !rit->second.parsed) {
                    ensure_model(state, lc);
                }
            }
        }
    }

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
                    fprintf(stderr, "Warning: Line %d: .tran requires tstep and tstop — skipping\n", line.line_number);
                    continue;
                }
                TranCmd tran;
                tran.tstep = parse_spice_number(tokens[1]);
                tran.tstop = parse_spice_number(tokens[2]);
                // Parse optional: tstart, tmax, uic
                // .tran tstep tstop [tstart [tmax]] [uic]
                for (size_t k = 3; k < tokens.size(); ++k) {
                    if (to_lower(tokens[k]) == "uic") {
                        tran.uic = true;
                    } else {
                        try {
                            double val = parse_spice_number(tokens[k]);
                            if (k == 3) tran.tstart = val;
                            else if (k == 4) tran.tmax = val;
                        } catch (const ParseError&) {
                            // Not a number and not "uic" — ignore
                        }
                    }
                }
                ckt.analyses.push_back(tran);
            } else if (first == ".ac") {
                if (tokens.size() < 5) {
                    fprintf(stderr, "Warning: Line %d: .ac requires mode npoints fstart fstop — skipping\n", line.line_number);
                    continue;
                }
                try {
                    ACCmd ac;
                    std::string mode = to_lower(tokens[1]);
                    if (mode == "dec") ac.mode = ACMode::DEC;
                    else if (mode == "oct") ac.mode = ACMode::OCT;
                    else if (mode == "lin") ac.mode = ACMode::LIN;
                    else { fprintf(stderr, "Warning: Line %d: Unknown AC mode '%s' — skipping\n", line.line_number, tokens[1].c_str()); continue; }
                    ac.npoints = static_cast<int>(parse_spice_number(tokens[2]));
                    ac.fstart = parse_spice_number(tokens[3]);
                    ac.fstop = parse_spice_number(tokens[4]);
                    ckt.analyses.push_back(ac);
                } catch (...) {
                    fprintf(stderr, "Warning: Line %d: .ac has invalid parameters — skipping\n", line.line_number);
                }
            } else if (first == ".noise") {
                // .noise V(out) Vin dec 10 1 1e9
                if (tokens.size() < 7) {
                    fprintf(stderr, "Warning: Line %d: .noise requires V(output) input_source mode npoints fstart fstop — skipping\n", line.line_number);
                    continue;
                }
                NoiseCmd ncmd;

                std::string out_spec = tokens[1];
                std::string out_lower = to_lower(out_spec);
                if (out_lower.size() > 3 && out_lower.substr(0, 2) == "v(" && out_lower.back() == ')') {
                    ncmd.output = out_lower.substr(2, out_lower.size() - 3);
                } else {
                    fprintf(stderr, "Warning: Line %d: .noise output must be V(node) — skipping\n", line.line_number);
                    continue;
                }

                ncmd.input_src = tokens[2];

                std::string mode = to_lower(tokens[3]);
                if (mode == "dec") ncmd.mode = ACMode::DEC;
                else if (mode == "oct") ncmd.mode = ACMode::OCT;
                else if (mode == "lin") ncmd.mode = ACMode::LIN;
                else { fprintf(stderr, "Warning: Line %d: Unknown noise sweep mode '%s' — skipping\n", line.line_number, tokens[3].c_str()); continue; }
                ncmd.npoints = static_cast<int>(parse_spice_number(tokens[4]));
                ncmd.fstart = parse_spice_number(tokens[5]);
                ncmd.fstop = parse_spice_number(tokens[6]);
                ckt.analyses.push_back(ncmd);
            } else if (first == ".dc") {
                // .dc Vsrc1 start1 stop1 step1 [Vsrc2 start2 stop2 step2]
                // Each sweep group is 4 tokens: source_name start stop step
                if (tokens.size() < 5) {
                    fprintf(stderr, "Warning: Line %d: .dc requires source start stop step — skipping\n", line.line_number);
                    continue;
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
                        fprintf(stderr, "Warning: Line %d: .dc sweep parameters must be numbers — skipping\n", line.line_number);
                        break;
                    }
                    dcmd.params.push_back(sp);
                    i += 4;
                    if (dcmd.params.size() >= 2) break; // support at most 2 sweeps
                }
                if (dcmd.params.empty()) {
                    fprintf(stderr, "Warning: Line %d: .dc requires at least one sweep group — skipping\n", line.line_number);
                    continue;
                }
                ckt.analyses.push_back(dcmd);
            } else if (first == ".options" || first == ".option") {
                for (size_t i = 1; i < tokens.size(); ++i) {
                    auto eq_pos = tokens[i].find('=');
                    if (eq_pos == std::string::npos) {
                        // Bare flag options (no '=' sign)
                        std::string flag = to_lower(tokens[i]);
                        if (flag == "interp") ckt.options.interp = true;
                        else if (flag == "newtrunc") ckt.options.newtrunc = true;
                        else if (flag == "nonewtrunc") ckt.options.newtrunc = false;
                        continue;
                    }
                    std::string key = to_lower(tokens[i].substr(0, eq_pos));
                    std::string val_str = tokens[i].substr(eq_pos + 1);
                    // method is a string option; all others are numeric
                    if (key == "method") {
                        ckt.options.method = to_lower(val_str);
                    } else {
                        try {
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
                            } else if (key == "itl2") {
                                ckt.options.itl2 = static_cast<int>(val);
                            } else if (key == "itl4") {
                                ckt.options.itl4 = static_cast<int>(val);
                            } else if (key == "lte_ref_mode") {
                                ckt.options.lte_ref_mode = static_cast<int>(val);
                            } else if (key == "restart_step_scale") {
                                ckt.options.restart_step_scale = val;
                            } else if (key == "xmu") {
                                ckt.options.xmu = std::max(0.0, std::min(0.5, val));
                            }
                        } catch (...) {
                            // Silently ignore non-numeric option values
                        }
                    }
                }
            } else if (first == ".temp") {
                if (tokens.size() >= 2) {
                    try {
                        ckt.options.temp = parse_spice_number(tokens[1]) + 273.15;
                    } catch (...) {}
                }
            } else if (first == ".ic") {
                // .ic V(node)=value ...
                for (size_t i = 1; i < tokens.size(); ++i) {
                    std::string tok = tokens[i];
                    auto eq_pos = tok.find('=');
                    if (eq_pos == std::string::npos) continue;
                    std::string lhs = tok.substr(0, eq_pos);
                    double val;
                    try {
                        val = parse_spice_number(tok.substr(eq_pos + 1));
                    } catch (...) { continue; }
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
                    double val;
                    try {
                        val = parse_spice_number(tok.substr(eq_pos + 1));
                    } catch (...) { continue; }
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
                    fprintf(stderr, "Warning: Line %d: .meas requires analysis_type, name, and measure specification — skipping\n", line.line_number);
                    continue;
                }
                try {
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
                    fprintf(stderr, "Warning: Line %d: Unknown .meas type: %s — skipping\n", line.line_number, tokens[3].c_str());
                    continue;
                }
                ckt.measures.push_back(std::move(mcmd));
                } catch (const ParseError&) {
                    fprintf(stderr, "Warning: Line %d: .meas parse error — skipping\n", line.line_number);
                }
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
                if (tokens.size() < 3) {
                    fprintf(stderr, "Warning: Line %d: .tf requires output variable and input source — skipping\n", line.line_number);
                    continue;
                }
                TFCmd tf;
                tf.output = to_lower(tokens[1]);
                tf.input_src = to_lower(tokens[2]);
                ckt.analyses.push_back(tf);
            } else if (first == ".sens") {
                if (tokens.size() < 2) {
                    fprintf(stderr, "Warning: Line %d: .sens requires output variable — skipping\n", line.line_number);
                    continue;
                }
                SensCmd sens;
                sens.output = to_lower(tokens[1]);
                ckt.analyses.push_back(sens);
            } else if (first == ".pz") {
                if (tokens.size() < 7) {
                    fprintf(stderr, "Warning: Line %d: .pz requires 6 arguments — skipping\n", line.line_number);
                    continue;
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
                else { fprintf(stderr, "Warning: Line %d: .pz transfer type must be VOL or CUR — skipping\n", line.line_number); continue; }
                std::string pz = to_lower(tokens[6]);
                if (pz == "pol")      pzcmd.type = PZType::POLES;
                else if (pz == "zer") pzcmd.type = PZType::ZEROS;
                else if (pz == "pz")  pzcmd.type = PZType::BOTH;
                else { fprintf(stderr, "Warning: Line %d: .pz type must be POL, ZER, or PZ — skipping\n", line.line_number); continue; }
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
                // .step param <name> LIST <v1> <v2> ...
                // .step <Vsource> <start> <stop> <step>
                // .step temp <start> <stop> <step>
                if (tokens.size() < 3) {
                    fprintf(stderr, "Warning: Line %d: .step requires arguments — skipping\n", line.line_number);
                    continue;
                }
                try {
                    StepCommand sc;
                    std::string kind_or_name = to_lower(tokens[1]);
                    if (kind_or_name == "param") {
                        sc.kind = StepCommand::PARAM;
                        if (tokens.size() < 4) {
                            fprintf(stderr, "Warning: Line %d: .step param requires more arguments — skipping\n", line.line_number);
                            continue;
                        }
                        sc.name = to_lower(tokens[2]);
                        if (tokens.size() >= 4 && to_lower(tokens[3]) == "list") {
                            // LIST form: just use the first value as start, ignored for now
                            if (tokens.size() >= 5) {
                                sc.start = parse_spice_number(tokens[4]);
                                sc.stop = sc.start;
                                sc.step = 1.0;
                            }
                        } else if (tokens.size() >= 6) {
                            sc.start = parse_spice_number(tokens[3]);
                            sc.stop  = parse_spice_number(tokens[4]);
                            sc.step  = parse_spice_number(tokens[5]);
                        } else {
                            fprintf(stderr, "Warning: Line %d: .step param requires name start stop step — skipping\n", line.line_number);
                            continue;
                        }
                    } else if (kind_or_name == "temp") {
                        if (tokens.size() < 5) {
                            fprintf(stderr, "Warning: Line %d: .step temp requires start stop step — skipping\n", line.line_number);
                            continue;
                        }
                        sc.kind  = StepCommand::TEMP;
                        sc.start = parse_spice_number(tokens[2]);
                        sc.stop  = parse_spice_number(tokens[3]);
                        sc.step  = parse_spice_number(tokens[4]);
                    } else {
                        if (tokens.size() < 5) {
                            fprintf(stderr, "Warning: Line %d: .step source requires start stop step — skipping\n", line.line_number);
                            continue;
                        }
                        sc.kind = StepCommand::SOURCE;
                        sc.name  = to_lower(tokens[1]);
                        sc.start = parse_spice_number(tokens[2]);
                        sc.stop  = parse_spice_number(tokens[3]);
                        sc.step  = parse_spice_number(tokens[4]);
                    }
                    ckt.step_commands.push_back(sc);
                } catch (...) {
                    fprintf(stderr, "Warning: Line %d: .step has invalid numeric parameters — skipping\n", line.line_number);
                }
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

        // Filter out lines that don't look like valid SPICE elements.
        // Valid element names: letter + alphanumeric/underscore/dot chars.
        // Comment continuation lines (e.g., "Inc.", "These", "International")
        // often contain periods at the end or are just words.
        {
            static const std::string valid_prefixes = "rvlcdikmqjzxefghbostw";
            if (valid_prefixes.find(elem_type) == std::string::npos) {
                continue; // not a known element type
            }
            // Skip bare words that ended with punctuation (comment text)
            if (!first.empty() && (first.back() == '.' || first.back() == ',' ||
                                    first.back() == ':' || first.back() == ';')) {
                continue;
            }
        }

        if (elem_type == 'r') {
            // R name n+ n- value [model] [TC1=val] [TC2=val] [SCALE=val] [TEMP=val] [DTEMP=val]
            if (tokens.size() < 4) {
                fprintf(stderr, "Warning: Line %d: Resistor requires name, n+, n-, value — skipping\n", line.line_number);
                continue;
            }
            std::string name = tokens[0];
            int32_t np = node_raw(tokens[1]);
            int32_t nn = node_raw(tokens[2]);
            double val = 0.0;
            size_t model_start = 4;
            try {
                val = parse_value(tokens[3]);
            } catch (const ParseError&) {
                // tokens[3] is not a number — treat as model name.
                // ngspice accepts both token orders:
                //   Rname n+ n- value MODEL   and   Rname n+ n- MODEL value
                // so when the model name comes first, the resistance is in a
                // later bare (non key=value) token. Also honor an r=<val> param.
                auto mit = res_models.find(to_lower(tokens[3]));
                if (mit != res_models.end()) {
                    val = 0.0; // resistance may follow the model name
                    for (size_t k = 4; k < tokens.size(); ++k) {
                        std::string tl = to_lower(tokens[k]);
                        if (tl.starts_with("r=")) {
                            try { val = parse_value(tokens[k].substr(2)); } catch (const ParseError&) {}
                            break;
                        }
                        if (tl.find('=') != std::string::npos) continue; // skip TC1=, etc.
                        try { val = parse_value(tokens[k]); break; } catch (const ParseError&) {}
                    }
                } else {
                    fprintf(stderr, "Warning: Line %d: Resistor '%s' has unrecognized value/model '%s' — skipping\n",
                            line.line_number, name.c_str(), tokens[3].c_str());
                    continue;
                }
                model_start = 4;
            }
            auto r = std::make_unique<Resistor>(name, np, nn, val);

            // First pass: apply model defaults (if model reference present)
            for (size_t k = 3; k < tokens.size(); ++k) {
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
                fprintf(stderr, "Warning: Line %d: Capacitor requires name, n+, n-, value — skipping\n", line.line_number);
                continue;
            }
            std::string name = tokens[0];
            int32_t np = node_raw(tokens[1]);
            int32_t nn = node_raw(tokens[2]);
            double val;
            try {
                val = parse_value(tokens[3]);
            } catch (const ParseError&) {
                fprintf(stderr, "Warning: Line %d: Capacitor '%s' has unrecognized value '%s' — skipping\n",
                        line.line_number, name.c_str(), tokens[3].c_str());
                continue;
            }
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
                fprintf(stderr, "Warning: Line %d: Inductor requires name, n+, n-, value — skipping\n", line.line_number);
                continue;
            }
            std::string name = tokens[0];
            int32_t np = node_raw(tokens[1]);
            int32_t nn = node_raw(tokens[2]);

            double val;
            size_t param_start;
            std::string tok3_lower = to_lower(tokens[3]);
            auto model_it = ind_models.find(tok3_lower);
            if (model_it != ind_models.end()) {
                if (tokens.size() < 5) {
                    fprintf(stderr, "Warning: Line %d: Inductor with model requires a value — skipping\n", line.line_number);
                    continue;
                }
                try {
                    val = parse_value(tokens[4]);
                } catch (const ParseError&) {
                    fprintf(stderr, "Warning: Line %d: Inductor '%s' has unrecognized value '%s' — skipping\n",
                            line.line_number, name.c_str(), tokens[4].c_str());
                    continue;
                }
                param_start = 5;
            } else {
                try {
                    val = parse_value(tokens[3]);
                } catch (const ParseError&) {
                    fprintf(stderr, "Warning: Line %d: Inductor '%s' has unrecognized value '%s' — skipping\n",
                            line.line_number, name.c_str(), tokens[3].c_str());
                    continue;
                }
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
                fprintf(stderr, "Warning: Line %d: VSource requires name, n+, n- — skipping\n", line.line_number);
                continue;
            }
            std::string name = tokens[0];
            int32_t np = node_raw(tokens[1]);
            int32_t nn = node_raw(tokens[2]);

            ParsedSourceSpec spec = parse_source_spec(tokens, 3);
            auto vs = std::make_unique<VSource>(name, np, nn, spec.dc_val);
            vs->set_dc_given(spec.dc_given);
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
                fprintf(stderr, "Warning: Line %d: ISource requires name, n+, n- — skipping\n", line.line_number);
                continue;
            }
            std::string name = tokens[0];
            int32_t np = node_raw(tokens[1]);
            int32_t nn = node_raw(tokens[2]);

            ParsedSourceSpec spec = parse_source_spec(tokens, 3);
            auto is = std::make_unique<ISource>(name, np, nn, spec.dc_val);
            is->set_dc_given(spec.dc_given);
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
                try {
                    auto elem = handler->parse(tokens, parse_ctx);
                    if (elem) {
                        elem->parse_order = state.next_element_order++;
                        parsed_elements[elem_type].push_back(std::move(elem));
                    }
                } catch (const ParseError& e) {
                    fprintf(stderr, "Warning: %s — skipping\n", e.what());
                    continue;
                }
            }

        } else if (elem_type == 'e') {
            // E name np nn [POLY(N) cp1 cn1 ... coeffs | TABLE {V(in)} = (x,y)... | nc+ nc- gain]
            if (tokens.size() < 4) {
                fprintf(stderr, "Warning: Line %d: VCVS requires name, np, nn, and source specification — skipping\n", line.line_number);
                continue;
            }
            std::string name = tokens[0];

            // Support parenthesized output nodes: E name (np,nn) ...
            size_t e_tok_offset = 0;
            int32_t np, nn;
            {
                std::string t1 = tokens[1];
                if (t1.size() > 1 && t1[0] == '(' && t1.find(',') != std::string::npos) {
                    // Parse (np,nn)
                    std::string inner = t1;
                    if (inner.front() == '(') inner.erase(0, 1);
                    if (inner.back() == ')') inner.pop_back();
                    auto comma = inner.find(',');
                    np = node_raw(inner.substr(0, comma));
                    nn = node_raw(inner.substr(comma + 1));
                    e_tok_offset = 1;  // tokens shifted by 1
                } else {
                    np = node_raw(tokens[1]);
                    nn = node_raw(tokens[2]);
                }
            }

            // Detect POLY or TABLE keyword at token[3] (adjusted for parenthesized nodes)
            std::string tok3 = to_lower(tokens[3 - e_tok_offset]);
            // Tolerate a stray leading '(' glued onto the POLY keyword (e.g.
            // "(POLY(1)" in some ADI macromodels). Strip it for keyword/dim
            // detection; the scanner below reads the ORIGINAL token, so track
            // the strip offset to keep scan_off aligned to the original string.
            size_t e_kw_strip = 0;
            if (tok3.size() > 1 && tok3[0] == '(' && tok3.compare(1, 4, "poly") == 0) {
                tok3.erase(0, 1);
                e_kw_strip = 1;
            }

            if (tok3.substr(0, 4) == "poly") {
                // POLY(N) form
                // Extract dimension N from "poly(n)" or "poly" followed by "(n)"
                int poly_dim = 1;
                bool poly_dim_in_token = false;
                std::string poly_tok = tok3;
                size_t paren_pos = poly_tok.find('(');
                if (paren_pos != std::string::npos) {
                    size_t close = poly_tok.find(')');
                    if (close != std::string::npos && close > paren_pos) {
                        poly_dim = std::stoi(poly_tok.substr(paren_pos + 1, close - paren_pos - 1));
                        poly_dim_in_token = true;
                    }
                } else if (tokens.size() > 4 - e_tok_offset) {
                    // "POLY (N)" — dimension in separate token
                    std::string next = tokens[4 - e_tok_offset];
                    if (!next.empty() && next.front() == '(') {
                        size_t close = next.find(')');
                        if (close != std::string::npos) {
                            poly_dim = std::stoi(next.substr(1, close - 1));
                        }
                    }
                }
                // Now parse 2*poly_dim control node atoms with the gettok_node
                // scanner so (cp,cn), (cp cn), bare cp cn, and the comma-glued
                // "POLY(N),(cp,cn),..." form all parse identically. When the
                // count is glued in the POLY token, resume scanning right after
                // its ')'; otherwise start at the token after POLY[/ "(N)"].
                size_t scan_tok = 3 - e_tok_offset;
                size_t scan_off = 0;
                if (poly_dim_in_token) {
                    // +e_kw_strip re-aligns to the original (unstripped) token.
                    scan_off = tok3.find(')') + 1 + e_kw_strip;  // resume after the count
                } else {
                    scan_tok = 4 - e_tok_offset;
                    if (scan_tok < tokens.size() && tokens[scan_tok].front() == '(')
                        ++scan_tok;  // skip separate "(N)" token
                }
                NodeAtomScanner psc(tokens, scan_tok, scan_off);
                std::vector<CtrlPair> ctrl_pairs;
                ctrl_pairs.reserve(poly_dim);
                bool poly_ok = true;
                for (int k = 0; k < poly_dim; ++k) {
                    std::string sp, sn;
                    if (!psc.next(sp) || !psc.next(sn)) {
                        fprintf(stderr, "Warning: Line %d: POLY VCVS: not enough control node pairs — skipping\n", line.line_number);
                        poly_ok = false;
                        break;
                    }
                    ctrl_pairs.push_back({node_raw(sp), node_raw(sn)});
                }
                size_t idx = psc.resume_token_index();
                if (!poly_ok) continue;
                // Remaining tokens are polynomial coefficients
                std::vector<double> coeffs;
                parse_poly_coeffs(tokens, idx, coeffs);
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
                    table_ctrl_token = tokens[3 - e_tok_offset].substr(5);  // use original case
                    table_data_start = 4 - e_tok_offset;
                } else {
                    if (tokens.size() < 6 - e_tok_offset) {
                        fprintf(stderr, "Warning: Line %d: TABLE VCVS requires control expression and table points — skipping\n", line.line_number);
                        continue;
                    }
                    table_ctrl_token = tokens[4 - e_tok_offset];
                    table_data_start = 5 - e_tok_offset;
                    // Multi-token expression: join tokens until closing '}'
                    if (table_ctrl_token.find('{') != std::string::npos &&
                        table_ctrl_token.find('}') == std::string::npos) {
                        for (size_t ti = table_data_start; ti < tokens.size(); ++ti) {
                            table_ctrl_token += tokens[ti];
                            table_data_start = ti + 1;
                            if (tokens[ti].find('}') != std::string::npos) break;
                        }
                    }
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
                std::vector<TablePoint> pts = parse_table_points(joined);
                if (pts.empty()) {
                    fprintf(stderr, "Warning: Line %d: TABLE VCVS: no table points found — skipping\n", line.line_number);
                    continue;
                }
                ckt.add_device(std::make_unique<TableVCVS>(
                    name, np, nn, ctrl_pos, ctrl_neg, std::move(pts)));

            } else if (tok3 == "value" || tok3.substr(0, 6) == "value=" ||
                       tok3.substr(0, 6) == "value{") {
                // PSpice VALUE={expr} form — lower to ASRCDevice with VOLTAGE mode
                // Extract expression from VALUE={expr} or VALUE = {expr} or VALUE ={expr}
                std::string expr_str;
                if (tok3.substr(0, 6) == "value{") {
                    // VALUE{expr} — brace attached directly; join remaining
                    // tokens from the first '{' (original case preserved).
                    std::string rest;
                    for (size_t i = 3 - e_tok_offset; i < tokens.size(); ++i) {
                        if (!rest.empty()) rest += ' ';
                        rest += tokens[i];
                    }
                    expr_str = rest.substr(rest.find('{'));
                } else if (tok3.substr(0, 6) == "value=") {
                    // VALUE={expr} as single token or VALUE=... — existing path
                    std::string rest;
                    for (size_t i = 3 - e_tok_offset; i < tokens.size(); ++i) {
                        if (!rest.empty()) rest += ' ';
                        rest += tokens[i];
                    }
                    expr_str = rest.substr(rest.find('=') + 1);
                } else {
                    // tok3 == "value", look for = or { in next tokens
                    size_t expr_start = 4 - e_tok_offset;
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
                    } else if (expr_start < tokens.size()) {
                        // PSpice: VALUE {expr} — no equals sign, expression follows directly
                        for (size_t i = expr_start; i < tokens.size(); ++i) {
                            if (!expr_str.empty()) expr_str += ' ';
                            expr_str += tokens[i];
                        }
                    } else {
                        fprintf(stderr, "Warning: Line %d: E element VALUE form requires VALUE={expr} — skipping\n", line.line_number);
                        continue;
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
                    fprintf(stderr, "Warning: Line %d: E element VALUE form has empty expression — skipping\n", line.line_number);
                    continue;
                }

                // Expand .func calls, then resolve any bare .param identifiers
                // so behavioral arithmetic (e.g. {1.63m - IEE}) folds to numeric
                // values (subckt-internal E/G already do this via the expander;
                // top-level E elements need it here too).
                expr_str = expand_funcs(expr_str, func_defs);
                expr_str = subst_param_names(expr_str, state.params);

                // Compile the expression
                asrc::CompiledExpression compiled;
                try {
                    compiled = asrc::CompiledExpression::compile(expr_str);
                } catch (const ParseError& e) {
                    fprintf(stderr, "Warning: Line %d: E element VALUE expression error: %s — skipping\n", line.line_number, e.what());
                    continue;
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
                // Linear form. Control nodes follow gettok_node semantics, so
                // all of "nc+ nc-", "(nc+,nc-)" and "(nc+ nc-)" parse alike.
                int32_t ncp, ncn;
                double gain;
                size_t gain_idx;
                {
                    NodeAtomScanner csc(tokens, 3 - e_tok_offset);
                    std::string s_ncp, s_ncn;
                    if (!csc.next(s_ncp) || !csc.next(s_ncn)) {
                        fprintf(stderr, "Warning: Line %d: VCVS requires name, np, nn, nc+, nc-, gain — skipping\n", line.line_number);
                        continue;
                    }
                    ncp = node_raw(s_ncp);
                    ncn = node_raw(s_ncn);
                    gain_idx = csc.resume_token_index();
                }

                if (gain_idx >= tokens.size()) {
                    fprintf(stderr, "Warning: Line %d: VCVS requires gain value — skipping\n", line.line_number);
                    continue;
                }
                try {
                    gain = parse_spice_number(tokens[gain_idx]);
                } catch (...) {
                    fprintf(stderr, "Warning: Line %d: VCVS '%s' has invalid gain — skipping\n", line.line_number, name.c_str());
                    continue;
                }
                // PSpice implicit polynomial: "E n+ n- nc+ nc- c0 c1 c2 ..."
                // with no POLY keyword is POLY(1) — V = c0 + c1·Vc + c2·Vc² + …
                std::vector<double> e_coeffs;
                e_coeffs.push_back(gain);
                for (size_t k = gain_idx + 1; k < tokens.size(); ++k) {
                    if (to_lower(tokens[k]).starts_with("m=")) break;
                    try { e_coeffs.push_back(parse_spice_number(tokens[k])); }
                    catch (...) { break; }
                }
                if (e_coeffs.size() > 1) {
                    std::vector<CtrlPair> ctrl_pairs{ {ncp, ncn} };
                    ckt.add_device(std::make_unique<NonlinearVCVS>(
                        name, np, nn, std::move(ctrl_pairs), std::move(e_coeffs)));
                } else {
                    ckt.add_device(std::make_unique<VCVS>(name, np, nn, ncp, ncn, gain));
                }
            }

        } else if (elem_type == 'g') {
            // G name np nn [POLY(N) cp1 cn1 ... coeffs | TABLE {V(in)} = (x,y)... | nc+ nc- gm]
            if (tokens.size() < 4) {
                fprintf(stderr, "Warning: Line %d: VCCS requires name, np, nn, and source specification — skipping\n", line.line_number);
                continue;
            }
            std::string name = tokens[0];

            // Support parenthesized output nodes: G name (np,nn) ...
            size_t g_tok_offset = 0;
            int32_t np, nn;
            {
                std::string t1 = tokens[1];
                if (t1.size() > 1 && t1[0] == '(' && t1.find(',') != std::string::npos) {
                    std::string inner = t1;
                    if (inner.front() == '(') inner.erase(0, 1);
                    if (inner.back() == ')') inner.pop_back();
                    auto comma = inner.find(',');
                    np = node_raw(inner.substr(0, comma));
                    nn = node_raw(inner.substr(comma + 1));
                    g_tok_offset = 1;
                } else {
                    np = node_raw(tokens[1]);
                    nn = node_raw(tokens[2]);
                }
            }

            std::string tok3g = to_lower(tokens[3 - g_tok_offset]);
            // Tolerate a stray leading '(' glued onto the POLY keyword (see the
            // E-element handling above for the rationale).
            size_t g_kw_strip = 0;
            if (tok3g.size() > 1 && tok3g[0] == '(' && tok3g.compare(1, 4, "poly") == 0) {
                tok3g.erase(0, 1);
                g_kw_strip = 1;
            }

            if (tok3g.substr(0, 4) == "poly") {
                // POLY(N) form for VCCS
                int poly_dim = 1;
                bool poly_dim_in_token = false;
                std::string poly_tok = tok3g;
                size_t paren_pos = poly_tok.find('(');
                if (paren_pos != std::string::npos) {
                    size_t close = poly_tok.find(')');
                    if (close != std::string::npos && close > paren_pos) {
                        poly_dim = std::stoi(poly_tok.substr(paren_pos + 1, close - paren_pos - 1));
                        poly_dim_in_token = true;
                    }
                } else if (tokens.size() > 4 - g_tok_offset) {
                    std::string next = tokens[4 - g_tok_offset];
                    if (!next.empty() && next.front() == '(') {
                        size_t close = next.find(')');
                        if (close != std::string::npos) {
                            poly_dim = std::stoi(next.substr(1, close - 1));
                        }
                    }
                }
                // Parse 2*poly_dim control node atoms via the gettok_node
                // scanner; handles the comma-glued "POLY(N),(cp,cn),..." form.
                size_t scan_tok = 3 - g_tok_offset;
                size_t scan_off = 0;
                if (poly_dim_in_token) {
                    scan_off = tok3g.find(')') + 1 + g_kw_strip;
                } else {
                    scan_tok = 4 - g_tok_offset;
                    if (scan_tok < tokens.size() && tokens[scan_tok].front() == '(')
                        ++scan_tok;
                }
                NodeAtomScanner psc(tokens, scan_tok, scan_off);
                std::vector<CtrlPair> ctrl_pairs;
                ctrl_pairs.reserve(poly_dim);
                bool poly_ok = true;
                for (int k = 0; k < poly_dim; ++k) {
                    std::string sp, sn;
                    if (!psc.next(sp) || !psc.next(sn)) {
                        fprintf(stderr, "Warning: Line %d: POLY VCCS: not enough control node pairs — skipping\n", line.line_number);
                        poly_ok = false;
                        break;
                    }
                    ctrl_pairs.push_back({node_raw(sp), node_raw(sn)});
                }
                size_t idx = psc.resume_token_index();
                if (!poly_ok) continue;
                std::vector<double> coeffs;
                parse_poly_coeffs(tokens, idx, coeffs);
                ckt.add_device(std::make_unique<NonlinearVCCS>(
                    name, np, nn, std::move(ctrl_pairs), std::move(coeffs)));

            } else if (tok3g == "table" || tok3g.substr(0, 6) == "table{") {
                // TABLE {V(node)} = (x,y) ...
                // Also handle PSpice-style TABLE{V(in)} (no space between TABLE and {)
                std::string table_ctrl_token;
                size_t table_data_start;
                if (tok3g.substr(0, 6) == "table{") {
                    table_ctrl_token = tokens[3 - g_tok_offset].substr(5);
                    table_data_start = 4 - g_tok_offset;
                } else {
                    if (tokens.size() < 6 - g_tok_offset) {
                        fprintf(stderr, "Warning: Line %d: TABLE VCCS requires control expression and table points — skipping\n", line.line_number);
                        continue;
                    }
                    table_ctrl_token = tokens[4 - g_tok_offset];
                    table_data_start = 5 - g_tok_offset;
                    // Multi-token expression: join tokens until closing '}'
                    if (table_ctrl_token.find('{') != std::string::npos &&
                        table_ctrl_token.find('}') == std::string::npos) {
                        for (size_t ti = table_data_start; ti < tokens.size(); ++ti) {
                            table_ctrl_token += tokens[ti];
                            table_data_start = ti + 1;
                            if (tokens[ti].find('}') != std::string::npos) break;
                        }
                    }
                }
                std::string ctrl_expr = table_ctrl_token;
                // The '=' separator between {expr} and the table points can be
                // glued to the control token (e.g. "{V(14,15)}="); strip it so
                // the braces below are removed and simple V() detection works.
                if (!ctrl_expr.empty() && ctrl_expr.back() == '=') ctrl_expr.pop_back();
                if (!ctrl_expr.empty() && ctrl_expr.front() == '{') ctrl_expr.erase(0, 1);
                if (!ctrl_expr.empty() && ctrl_expr.back() == '}') ctrl_expr.pop_back();
                std::string ctrl_lower = to_lower(ctrl_expr);

                // Detect simple V(node) or V(n1,n2) vs complex expression
                bool is_simple_v = false;
                int32_t ctrl_pos = GROUND_INTERNAL, ctrl_neg = GROUND_INTERNAL;
                if (ctrl_lower.size() > 3 && ctrl_lower[0] == 'v' && ctrl_lower[1] == '(') {
                    size_t close = ctrl_lower.find(')');
                    if (close != std::string::npos && close == ctrl_lower.size() - 1) {
                        is_simple_v = true;
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

                // Parse table points
                size_t idx = table_data_start;
                if (idx < tokens.size() && tokens[idx] == "=") ++idx;

                std::string joined;
                for (size_t i = idx; i < tokens.size(); ++i) {
                    joined += tokens[i];
                    joined += ' ';
                }
                std::vector<TablePoint> pts = parse_table_points(joined);
                if (pts.empty()) {
                    fprintf(stderr, "Warning: Line %d: TABLE VCCS: no table points found — skipping\n", line.line_number);
                    continue;
                }

                if (is_simple_v) {
                    ckt.add_device(std::make_unique<TableVCCS>(
                        name, np, nn, ctrl_pos, ctrl_neg, std::move(pts)));
                } else {
                    // Complex expression — compile and defer for I() resolution
                    ctrl_expr = expand_funcs(ctrl_expr, func_defs);
                    asrc::CompiledExpression compiled;
                    try {
                        compiled = asrc::CompiledExpression::compile(ctrl_expr);
                    } catch (const ParseError& e) {
                        fprintf(stderr, "Warning: Line %d: TABLE VCCS expression error: %s — skipping\n", line.line_number, e.what());
                        continue;
                    }
                    const auto& refs = compiled.var_refs();
                    int nv = compiled.num_vars();
                    std::vector<int32_t> t_node_indices(nv, -1);
                    std::vector<int32_t> t_node_indices2(nv, -1);
                    std::vector<std::string> t_vsrc_names(nv);

                    for (int i = 0; i < nv; ++i) {
                        const auto& ref = refs[i];
                        switch (ref.kind) {
                        case asrc::VarKind::NODE_VOLTAGE: {
                            std::string lname = ref.name1;
                            t_node_indices[i] = (lname == "0" || lname == "gnd")
                                ? GROUND_INTERNAL : node_raw(lname);
                            break;
                        }
                        case asrc::VarKind::DIFF_VOLTAGE: {
                            std::string ln1 = ref.name1, ln2 = ref.name2;
                            t_node_indices[i]  = (ln1 == "0" || ln1 == "gnd")
                                ? GROUND_INTERNAL : node_raw(ln1);
                            t_node_indices2[i] = (ln2 == "0" || ln2 == "gnd")
                                ? GROUND_INTERNAL : node_raw(ln2);
                            break;
                        }
                        case asrc::VarKind::BRANCH_CURRENT:
                            t_vsrc_names[i] = ref.name1;
                            break;
                        }
                    }

                    ParseState::DeferredTableVCCS td;
                    td.name = name;
                    td.np = np;
                    td.nn = nn;
                    td.expr = std::move(compiled);
                    td.node_indices = std::move(t_node_indices);
                    td.node_indices2 = std::move(t_node_indices2);
                    td.vsrc_names = std::move(t_vsrc_names);
                    td.table_points = std::move(pts);
                    td.line_number = line.line_number;
                    deferred_table_vccs.push_back(std::move(td));
                }

            } else if (tok3g == "value" || tok3g.substr(0, 6) == "value=" ||
                       tok3g.substr(0, 6) == "value{") {
                // PSpice VALUE={expr} form — lower to ASRCDevice with CURRENT mode
                // Extract expression from VALUE={expr} or VALUE = {expr} or VALUE ={expr}
                std::string expr_str;
                if (tok3g.substr(0, 6) == "value{") {
                    std::string rest;
                    for (size_t i = 3 - g_tok_offset; i < tokens.size(); ++i) {
                        if (!rest.empty()) rest += ' ';
                        rest += tokens[i];
                    }
                    expr_str = rest.substr(rest.find('{'));
                } else if (tok3g.substr(0, 6) == "value=") {
                    // VALUE={expr} as single token or VALUE=... — existing path
                    std::string rest;
                    for (size_t i = 3 - g_tok_offset; i < tokens.size(); ++i) {
                        if (!rest.empty()) rest += ' ';
                        rest += tokens[i];
                    }
                    expr_str = rest.substr(rest.find('=') + 1);
                } else {
                    // tok3g == "value", look for = or { in next tokens
                    size_t expr_start = 4 - g_tok_offset;
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
                    } else if (expr_start < tokens.size()) {
                        // PSpice: VALUE {expr} — no equals sign
                        for (size_t i = expr_start; i < tokens.size(); ++i) {
                            if (!expr_str.empty()) expr_str += ' ';
                            expr_str += tokens[i];
                        }
                    } else {
                        fprintf(stderr, "Warning: Line %d: G element VALUE form requires VALUE={expr} — skipping\n", line.line_number);
                        continue;
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
                    fprintf(stderr, "Warning: Line %d: G element VALUE form has empty expression — skipping\n", line.line_number);
                    continue;
                }

                expr_str = expand_funcs(expr_str, func_defs);
                expr_str = subst_param_names(expr_str, state.params);

                asrc::CompiledExpression compiled;
                try {
                    compiled = asrc::CompiledExpression::compile(expr_str);
                } catch (const ParseError& e) {
                    fprintf(stderr, "Warning: Line %d: G element VALUE expression error: %s — skipping\n", line.line_number, e.what());
                    continue;
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
                // Linear form. Control nodes follow gettok_node semantics, so
                // all of "nc+ nc-", "(nc+,nc-)" and "(nc+ nc-)" parse alike.
                int32_t ncp, ncn;
                double gm;
                size_t gm_idx;
                {
                    NodeAtomScanner csc(tokens, 3 - g_tok_offset);
                    std::string s_ncp, s_ncn;
                    if (!csc.next(s_ncp) || !csc.next(s_ncn)) {
                        fprintf(stderr, "Warning: Line %d: VCCS requires name, np, nn, nc+, nc-, gm — skipping\n", line.line_number);
                        continue;
                    }
                    ncp = node_raw(s_ncp);
                    ncn = node_raw(s_ncn);
                    gm_idx = csc.resume_token_index();
                }

                if (gm_idx >= tokens.size()) {
                    fprintf(stderr, "Warning: Line %d: VCCS requires gm value — skipping\n", line.line_number);
                    continue;
                }
                try {
                    gm = parse_spice_number(tokens[gm_idx]);
                } catch (...) {
                    fprintf(stderr, "Warning: Line %d: VCCS '%s' has invalid gm — skipping\n", line.line_number, name.c_str());
                    continue;
                }
                // PSpice implicit polynomial: "G n+ n- nc+ nc- c0 c1 c2 ..."
                // with no POLY keyword is POLY(1) — I = c0 + c1·Vc + c2·Vc² + …
                // Collect all trailing numeric coefficients; if more than one,
                // build a polynomial VCCS instead of a linear (single-gain) one.
                std::vector<double> g_coeffs;
                g_coeffs.push_back(gm);
                for (size_t k = gm_idx + 1; k < tokens.size(); ++k) {
                    if (to_lower(tokens[k]).starts_with("m=")) break;
                    try { g_coeffs.push_back(parse_spice_number(tokens[k])); }
                    catch (...) { break; }
                }
                if (g_coeffs.size() > 1) {
                    std::vector<CtrlPair> ctrl_pairs{ {ncp, ncn} };
                    ckt.add_device(std::make_unique<NonlinearVCCS>(
                        name, np, nn, std::move(ctrl_pairs), std::move(g_coeffs)));
                } else {
                    auto vccs = std::make_unique<VCCS>(name, np, nn, ncp, ncn, gm);
                    for (size_t k = gm_idx + 1; k < tokens.size(); ++k) {
                        std::string tok = to_lower(tokens[k]);
                        if (tok.starts_with("m="))
                            vccs->set_multiplier(parse_spice_number(tok.substr(2)));
                    }
                    ckt.add_device(std::move(vccs));
                }
            }

        } else if (elem_type == 'h') {
            // H name np nn [POLY(N) Vs1 ... coeffs | Vsense transresistance]
            if (tokens.size() < 5) {
                fprintf(stderr, "Warning: Line %d: CCVS requires name, np, nn, Vsense, transresistance — skipping\n", line.line_number);
                continue;
            }
            // Output nodes may be a parenthesized group "(n+,n-)" — flatten.
            std::vector<std::string> htoks_storage;
            const std::vector<std::string>* htoks = &tokens;
            if (tokens[1].size() > 1 && tokens[1][0] == '(') {
                NodeAtomScanner hsc(tokens, 1);
                std::string hnp, hnn;
                if (hsc.next(hnp) && hsc.next(hnn)) {
                    htoks_storage.push_back(tokens[0]);
                    htoks_storage.push_back(hnp);
                    htoks_storage.push_back(hnn);
                    for (size_t i = hsc.resume_token_index(); i < tokens.size(); ++i)
                        htoks_storage.push_back(tokens[i]);
                    htoks = &htoks_storage;
                }
            }
            const std::vector<std::string>& tokens = *htoks;
            std::string tok3h = to_lower(tokens[3]);
            if (tok3h.substr(0, 4) == "poly") {
                // POLY(N) form
                int poly_dim = 1;
                bool poly_dim_in_token = false;
                std::string poly_tok = tok3h;
                size_t paren_pos = poly_tok.find('(');
                if (paren_pos != std::string::npos) {
                    size_t close = poly_tok.find(')');
                    if (close != std::string::npos && close > paren_pos) {
                        poly_dim = std::stoi(poly_tok.substr(paren_pos + 1, close - paren_pos - 1));
                        poly_dim_in_token = true;
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
                // Parse N VSource names via NodeAtomScanner so that parenthesised
                // and comma-glued forms (e.g. POLY(1),(V1) or POLY(1) (V1)) are
                // handled identically to ngspice's MIFgettok, which treats
                // ( ) , as whitespace.
                size_t scan_tok, scan_off;
                if (poly_dim_in_token) {
                    // VS names may follow immediately after the count inside
                    // tokens[3]: "POLY(1),(V1)" — scan_off points past the ')'.
                    scan_tok = 3;
                    scan_off = tok3h.find(')') + 1;
                } else {
                    // Separate "(N)" token already consumed by dim-extraction.
                    scan_tok = 4;
                    scan_off = 0;
                    if (scan_tok < tokens.size() && !tokens[scan_tok].empty()
                            && tokens[scan_tok].front() == '(')
                        ++scan_tok;
                }
                NodeAtomScanner vsc(tokens, scan_tok, scan_off);
                std::vector<std::string> vsense_names;
                vsense_names.reserve(poly_dim);
                for (int k = 0; k < poly_dim; ++k) {
                    std::string vs;
                    if (!vsc.next(vs)) {
                        fprintf(stderr, "Warning: Line %d: POLY CCVS: not enough sensing VSource names — skipping\n", line.line_number);
                        break;
                    }
                    vsense_names.push_back(vs);
                }
                size_t idx = vsc.resume_token_index();
                if ((int)vsense_names.size() < poly_dim) continue;
                std::vector<double> coeffs;
                parse_poly_coeffs(tokens, idx, coeffs);
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
                try {
                    DeferredCCVS hd;
                    hd.name        = tokens[0];
                    hd.np          = node_raw(tokens[1]);
                    hd.nn          = node_raw(tokens[2]);
                    hd.vsense_name = tokens[3];
                    hd.rm          = parse_spice_number(tokens[4]);
                    hd.line_number = line.line_number;
                    deferred_ccvs.push_back(std::move(hd));
                } catch (...) {
                    fprintf(stderr, "Warning: Line %d: CCVS '%s' has invalid transresistance — skipping\n", line.line_number, tokens[0].c_str());
                }
            }

        } else if (elem_type == 'f') {
            // F name np nn [POLY(N) Vs1 ... coeffs | Vsense gain]
            if (tokens.size() < 5) {
                fprintf(stderr, "Warning: Line %d: CCCS requires name, np, nn, Vsense, gain — skipping\n", line.line_number);
                continue;
            }
            // Output nodes may be a parenthesized group "(n+,n-)" (cadlab). When
            // so, build a flattened token list with the two output node atoms
            // spliced in as separate tokens so the by-position logic below works.
            std::vector<std::string> ftoks_storage;
            const std::vector<std::string>* ftoks = &tokens;
            if (tokens[1].size() > 1 && tokens[1][0] == '(') {
                NodeAtomScanner fsc(tokens, 1);
                std::string fnp, fnn;
                if (fsc.next(fnp) && fsc.next(fnn)) {
                    ftoks_storage.push_back(tokens[0]);
                    ftoks_storage.push_back(fnp);
                    ftoks_storage.push_back(fnn);
                    for (size_t i = fsc.resume_token_index(); i < tokens.size(); ++i)
                        ftoks_storage.push_back(tokens[i]);
                    ftoks = &ftoks_storage;
                }
            }
            const std::vector<std::string>& tokens = *ftoks;
            std::string tok3f = to_lower(tokens[3]);
            if (tok3f.substr(0, 4) == "poly") {
                // POLY(N) form
                int poly_dim = 1;
                bool poly_dim_in_token = false;
                std::string poly_tok = tok3f;
                size_t paren_pos = poly_tok.find('(');
                if (paren_pos != std::string::npos) {
                    size_t close = poly_tok.find(')');
                    if (close != std::string::npos && close > paren_pos) {
                        poly_dim = std::stoi(poly_tok.substr(paren_pos + 1, close - paren_pos - 1));
                        poly_dim_in_token = true;
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
                // Parse N VSource names via NodeAtomScanner so that parenthesised
                // and comma-glued forms (e.g. POLY(1),(V1) or POLY(1) (V1)) are
                // handled identically to ngspice's MIFgettok, which treats
                // ( ) , as whitespace.
                size_t scan_tok, scan_off;
                if (poly_dim_in_token) {
                    // VS names may follow immediately after the count inside
                    // tokens[3]: "POLY(1),(V1)" — scan_off points past the ')'.
                    scan_tok = 3;
                    scan_off = tok3f.find(')') + 1;
                } else {
                    // Separate "(N)" token already consumed by dim-extraction.
                    scan_tok = 4;
                    scan_off = 0;
                    if (scan_tok < tokens.size() && !tokens[scan_tok].empty()
                            && tokens[scan_tok].front() == '(')
                        ++scan_tok;
                }
                NodeAtomScanner vsc(tokens, scan_tok, scan_off);
                std::vector<std::string> vsense_names;
                vsense_names.reserve(poly_dim);
                for (int k = 0; k < poly_dim; ++k) {
                    std::string vs;
                    if (!vsc.next(vs)) {
                        fprintf(stderr, "Warning: Line %d: POLY CCCS: not enough sensing VSource names — skipping\n", line.line_number);
                        break;
                    }
                    vsense_names.push_back(vs);
                }
                size_t idx = vsc.resume_token_index();
                if ((int)vsense_names.size() < poly_dim) continue;
                std::vector<double> coeffs;
                parse_poly_coeffs(tokens, idx, coeffs);
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
                try {
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
                } catch (...) {
                    fprintf(stderr, "Warning: Line %d: CCCS '%s' has invalid gain — skipping\n", line.line_number, tokens[0].c_str());
                }
            }

        } else if (elem_type == 'k') {
            // K name L1 L2 [L3 ...] coupling_coefficient
            if (tokens.size() < 4) {
                fprintf(stderr, "Warning: Line %d: K element requires name, L1, L2, coupling_coefficient — skipping\n", line.line_number);
                continue;
            }
            // Last token is the coupling coefficient; everything between
            // tokens[1] and the last token are inductor names.
            double coupling = 0.0;
            try {
                coupling = parse_spice_number(tokens.back());
            } catch (...) {
                fprintf(stderr, "Warning: Line %d: K element '%s' has invalid coupling coefficient '%s' — skipping\n",
                        line.line_number, tokens[0].c_str(), tokens.back().c_str());
                continue;
            }
            std::vector<std::string> ind_names;
            for (size_t k = 1; k + 1 < tokens.size(); ++k) {
                ind_names.push_back(tokens[k]);
            }
            // Generate pairwise coupling entries
            for (size_t a = 0; a < ind_names.size(); ++a) {
                for (size_t b = a + 1; b < ind_names.size(); ++b) {
                    DeferredCoupledInductor kd;
                    kd.name = tokens[0];
                    kd.l1_name = ind_names[a];
                    kd.l2_name = ind_names[b];
                    kd.coupling = coupling;
                    kd.line_number = line.line_number;
                    deferred_coupled_inductors.push_back(std::move(kd));
                }
            }

        } else if (elem_type == 't') {
            // T name p1+ p1- p2+ p2- Z0=val TD=val
            if (tokens.size() < 6) {
                fprintf(stderr, "Warning: Line %d: T element requires name, p1+, p1-, p2+, p2-, Z0=... TD=... — skipping\n", line.line_number);
                continue;
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
                    std::string val_str = tokens[i].substr(eq + 1);
                    double val;
                    try {
                        val = parse_spice_number(val_str);
                    } catch (...) {
                        try {
                            val = eval_expression(val_str, state.params);
                        } catch (...) {
                            fprintf(stderr, "Warning: Line %d: T element '%s' cannot evaluate '%s=%s' — skipping\n",
                                    line.line_number, tname.c_str(), key.c_str(), val_str.c_str());
                            continue;
                        }
                    }
                    if      (key == "z0" || key == "zo") { tz0 = val; z0_given = true; }
                    else if (key == "td") { ttd = val; }
                    else if (key == "f")  { tf  = val; }
                    else if (key == "nl") { tnl = val; }
                }
            }
            if (!z0_given) {
                fprintf(stderr, "Warning: Line %d: T element '%s' missing Z0= — skipping\n", line.line_number, tname.c_str());
                continue;
            }
            if (tz0 <= 0.0) {
                fprintf(stderr, "Warning: Line %d: T element '%s' Z0 must be positive — skipping\n", line.line_number, tname.c_str());
                continue;
            }
            if (ttd < 0.0) {
                if (tf > 0.0 && tnl > 0.0) {
                    ttd = tnl / tf;
                } else if (tf > 0.0 && tnl < 0.0) {
                    ttd = 0.25 / tf;
                } else {
                    fprintf(stderr, "Warning: Line %d: T element '%s' requires TD= or F= — skipping\n", line.line_number, tname.c_str());
                    continue;
                }
            }
            auto tl = std::make_unique<TransmissionLine>(tname, tp1p, tp1n, tp2p, tp2n, tz0, ttd);
            if (ic_given)
                tl->set_ic(ic_v1, ic_i1, ic_v2, ic_i2);
            ckt.add_device(std::move(tl));

        } else if (elem_type == 'o') {
            // O name p1+ p1- p2+ p2- modelname [IC=v1,i1,v2,i2]
            if (tokens.size() < 6) {
                fprintf(stderr, "Warning: Line %d: O element requires name, p1+, p1-, p2+, p2-, modelname — skipping\n", line.line_number);
                continue;
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
            // Node lists may use ngspice gettok_node forms: bare "n+ n-",
            // comma/paren pairs "(n+,n-)", space-in-parens "(n+ n-)", or two
            // separate parenthesized groups "(n+,n-) (nc+,nc-)" (cadlab VSWITCH).
            // Minimum valid paren form is "name (n+,n-) (nc+,nc-) model" = 4
            // tokens; the NodeAtomScanner below extracts the four node atoms and
            // errors out itself if it cannot, so this guard only rejects
            // hopelessly short lines. (The W element below uses the same logic.)
            if (tokens.size() < 4) {
                fprintf(stderr, "Warning: Line %d: S element requires name, n+, n-, nc+, nc-, modelname — skipping\n", line.line_number);
                continue;
            }
            NodeAtomScanner s_scan(tokens, 1);
            std::string s_np, s_nn, s_ncp, s_ncn;
            if (!s_scan.next(s_np) || !s_scan.next(s_nn) ||
                !s_scan.next(s_ncp) || !s_scan.next(s_ncn)) {
                fprintf(stderr, "Warning: Line %d: S element requires name, n+, n-, nc+, nc-, modelname — skipping\n", line.line_number);
                continue;
            }
            size_t s_model_idx = s_scan.resume_token_index();
            if (s_model_idx >= tokens.size()) {
                fprintf(stderr, "Warning: Line %d: S element requires a model name — skipping\n", line.line_number);
                continue;
            }
            DeferredVSwitch sd;
            sd.name       = tokens[0];
            sd.np         = node_raw(s_np);
            sd.nn         = node_raw(s_nn);
            sd.ncp        = node_raw(s_ncp);
            sd.ncn        = node_raw(s_ncn);
            sd.model_name = tokens[s_model_idx];
            sd.line_number = line.line_number;
            deferred_vswitches.push_back(std::move(sd));

        } else if (elem_type == 'w') {
            // W name n+ n- Vsense modelname
            if (tokens.size() < 5) {
                fprintf(stderr, "Warning: Line %d: W element requires name, n+, n-, Vsense, modelname — skipping\n", line.line_number);
                continue;
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
                fprintf(stderr, "Warning: Line %d: B element requires name, np, nn, V={expr} or I={expr} — skipping\n", line.line_number);
                continue;
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

            // Accept "V=expr" / "I=expr" with optional whitespace around '='
            // (e.g. "V = 1e-3 + V(a,b)") as PSpice/ngspice allow.
            bool b_ok = false;
            if (!rest_lower.empty() && (rest_lower[0] == 'v' || rest_lower[0] == 'i')) {
                size_t p = 1;
                while (p < rest.size() &&
                       std::isspace(static_cast<unsigned char>(rest[p]))) ++p;
                if (p < rest.size() && rest[p] == '=') {
                    mode = (rest_lower[0] == 'v') ? ASRCDevice::Mode::VOLTAGE
                                                  : ASRCDevice::Mode::CURRENT;
                    expr_str = rest.substr(p + 1);
                    size_t s = expr_str.find_first_not_of(" \t");
                    expr_str = (s == std::string::npos) ? "" : expr_str.substr(s);
                    b_ok = true;
                }
            }
            if (!b_ok) {
                fprintf(stderr, "Warning: Line %d: B element requires V={expr} or I={expr}, got '%s' — skipping\n", line.line_number, rest.c_str());
                continue;
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
                fprintf(stderr, "Warning: Line %d: B element expression error: %s — skipping\n", line.line_number, e.what());
                continue;
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
    auto& deferred_table_vccs = state.deferred_table_vccs;
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
            fprintf(stderr, "Warning: Line %d: CCVS '%s' references unknown voltage source '%s' — skipping\n",
                    hd.line_number, hd.name.c_str(), hd.vsense_name.c_str());
            continue;
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
            fprintf(stderr, "Warning: Line %d: CCCS '%s' references unknown voltage source '%s' — skipping\n",
                    fd.line_number, fd.name.c_str(), fd.vsense_name.c_str());
            continue;
        }
        auto cccs = std::make_unique<CCCS>(fd.name, fd.np, fd.nn, fd.gain, vs);
        if (fd.m != 1.0) cccs->set_multiplier(fd.m);
        ckt.add_device(std::move(cccs));
    }

    // Resolve deferred POLY CCVS (H POLY elements) — find sensing VSources by name.
    for (const auto& hpd : deferred_poly_ccvs) {
        std::vector<const VSource*> vsenses;
        vsenses.reserve(hpd.vsense_names.size());
        bool poly_ccvs_ok = true;
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
                fprintf(stderr, "Warning: Line %d: POLY CCVS '%s' references unknown voltage source '%s' — skipping\n",
                        hpd.line_number, hpd.name.c_str(), vsname.c_str());
                poly_ccvs_ok = false;
                break;
            }
            vsenses.push_back(vs);
        }
        if (!poly_ccvs_ok) continue;
        ckt.add_device(std::make_unique<NonlinearCCVS>(
            hpd.name, hpd.np, hpd.nn, std::move(vsenses), hpd.coeffs));
    }

    // Resolve deferred POLY CCCS (F POLY elements) — find sensing VSources by name.
    for (const auto& fpd : deferred_poly_cccs) {
        std::vector<const VSource*> vsenses;
        vsenses.reserve(fpd.vsense_names.size());
        bool poly_cccs_ok = true;
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
                fprintf(stderr, "Warning: Line %d: POLY CCCS '%s' references unknown voltage source '%s' — skipping\n",
                        fpd.line_number, fpd.name.c_str(), vsname.c_str());
                poly_cccs_ok = false;
                break;
            }
            vsenses.push_back(vs);
        }
        if (!poly_cccs_ok) continue;
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
            fprintf(stderr, "Warning: Line %d: K element '%s' references unknown inductor '%s' — skipping\n",
                    kd.line_number, kd.name.c_str(), kd.l1_name.c_str());
            continue;
        }
        if (!l2) {
            fprintf(stderr, "Warning: Line %d: K element '%s' references unknown inductor '%s' — skipping\n",
                    kd.line_number, kd.name.c_str(), kd.l2_name.c_str());
            continue;
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
                    ": S element references unknown model '" + sd.model_name + "'");
            }
            it = it2;
        }
        std::string model_type = to_lower(it->second.type);
        if (model_type != "sw") {
            fprintf(stderr, "Warning: Line %d: S element references non-SW model '%s' — skipping\n",
                    sd.line_number, sd.model_name.c_str());
            continue;
        }
        SwitchModel sm = to_switch_model(it->second);
        ckt.add_device(std::make_unique<VSwitch>(
            sd.name, sd.np, sd.nn, sd.ncp, sd.ncn, sm));
    }

    // Resolve deferred CSwitch (W elements)
    for (const auto& wd : deferred_cswitches) {
        auto it = models.find(wd.model_name);
        if (it == models.end()) {
            auto it2 = models.find(to_lower(wd.model_name));
            if (it2 == models.end()) {
                throw ParseError("Line " + std::to_string(wd.line_number) +
                    ": W element references unknown model '" + wd.model_name + "'");
            }
            it = it2;
        }
        std::string model_type = to_lower(it->second.type);
        if (model_type != "csw") {
            fprintf(stderr, "Warning: Line %d: W element references non-CSW model '%s' — skipping\n",
                    wd.line_number, wd.model_name.c_str());
            continue;
        }
        SwitchModel sm = to_switch_model(it->second);

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
            fprintf(stderr, "Warning: Line %d: W element '%s' references unknown voltage source '%s' — skipping\n",
                    wd.line_number, wd.name.c_str(), wd.vsense_name.c_str());
            continue;
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
                    ": O element references unknown model '" + ol.model_name + "'");
            }
            it = it2;
        }
        std::string model_type = to_lower(it->second.type);
        if (model_type != "ltra") {
            fprintf(stderr, "Warning: Line %d: O element references non-LTRA model '%s' — skipping\n",
                    ol.line_number, ol.model_name.c_str());
            continue;
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
                fprintf(stderr, "Warning: Line %d: Invalid LTRA model parameters for '%s' — skipping\n",
                        ol.line_number, ol.model_name.c_str());
                continue;
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

    // Resolve deferred ASRC (B elements) — find VSource pointers for I() refs.
    // I() may also reference a non-source 2-terminal device (e.g. a resistor);
    // ngspice supports that. We splice a 0V current-sense source in series with
    // the device and bind I() to it. rsense_sources caches one sense source per
    // device so multiple references reuse it.
    std::unordered_map<std::string, const VSource*> rsense_sources;
    auto sense_source_for = [&](const std::string& devname) -> const VSource* {
        std::string key = to_lower(devname);
        if (auto it = rsense_sources.find(key); it != rsense_sources.end())
            return it->second;
        for (auto& dev : ckt.devices()) {
            auto* r = dynamic_cast<Resistor*>(dev.get());
            if (r && to_lower(r->name()) == key) {
                int32_t orig_nn = r->neg_node();
                int32_t sense_node = node_raw("__isns_" + devname);
                r->set_neg_node(sense_node);
                auto sense = std::make_unique<VSource>(
                    "__visns_" + devname, sense_node, orig_nn, 0.0);
                const VSource* sp = sense.get();
                ckt.add_device(std::move(sense));
                rsense_sources[key] = sp;
                return sp;
            }
        }
        return nullptr;
    };
    // Resolve I(name) to a branch-current-carrying device. Search order matches
    // ngspice: an independent voltage source, then any device that owns an MNA
    // branch variable (E/VCVS, H/CCVS, voltage-mode behavioral B/E/G sources,
    // inductors), and finally fall back to splicing a 0V sense source in series
    // with a 2-terminal device (e.g. a resistor) that has no branch of its own.
    auto branch_provider_for = [&](const std::string& name) -> const Device* {
        std::string key = to_lower(name);
        const Device* vsrc = nullptr;     // independent V source has priority
        const Device* branchdev = nullptr;  // any other branch-carrying device
        for (const auto& dev : ckt.devices()) {
            if (to_lower(dev->name()) != key) continue;
            if (dynamic_cast<const VSource*>(dev.get())) { vsrc = dev.get(); break; }
            // Owns a branch variable (E/H/L/voltage-mode B). branch_index() is
            // not yet assigned here, but extra_vars()>0 marks branch ownership.
            if (dev->extra_vars() > 0) branchdev = dev.get();
        }
        if (vsrc) return vsrc;
        if (branchdev) return branchdev;
        return sense_source_for(name);  // splice 0V sense (resistor fallback)
    };
    for (auto& bd : deferred_asrcs) {
        const auto& refs = bd.expr.var_refs();
        int nv = bd.expr.num_vars();
        std::vector<const Device*> vsource_ptrs(nv, nullptr);

        bool asrc_ok = true;
        // Indices whose I() target is this very (voltage-mode) source — a
        // self-reference. The device cannot be found via ckt.devices() because
        // it is not yet added; it owns its own branch, so wire it after build.
        std::vector<int> self_ref_idx;
        std::string bd_key = to_lower(bd.name);
        for (int i = 0; i < nv; ++i) {
            if (refs[i].kind == asrc::VarKind::BRANCH_CURRENT &&
                !bd.vsrc_names[i].empty()) {
                if (bd.mode == ASRCDevice::Mode::VOLTAGE &&
                    to_lower(bd.vsrc_names[i]) == bd_key) {
                    // I(self): voltage-mode ASRC owns a branch variable.
                    self_ref_idx.push_back(i);
                    continue;
                }
                const Device* vs = branch_provider_for(bd.vsrc_names[i]);
                if (!vs) {
                    fprintf(stderr, "Warning: Line %d: B element '%s' references unknown voltage source '%s' in I() — skipping\n",
                            bd.line_number, bd.name.c_str(), bd.vsrc_names[i].c_str());
                    asrc_ok = false;
                    break;
                }
                vsource_ptrs[i] = vs;
            }
        }
        if (!asrc_ok) continue;

        auto asrc_dev = std::make_unique<ASRCDevice>(
            bd.name, bd.np, bd.nn, bd.mode,
            std::move(bd.expr),
            std::move(bd.node_indices),
            std::move(bd.node_indices2),
            std::move(vsource_ptrs));

        // Wire self-referencing I(self) branch reads to this device itself.
        for (int idx : self_ref_idx)
            asrc_dev->set_vsource_ptr(idx, asrc_dev.get());

        // Apply temperature coefficient parameters
        asrc_dev->set_tc1(bd.tc1);
        asrc_dev->set_tc2(bd.tc2);
        if (bd.temp > 0) asrc_dev->set_temp(bd.temp);
        asrc_dev->set_dtemp(bd.dtemp);

        ckt.add_device(std::move(asrc_dev));
    }

    // Resolve deferred TABLE VCCS (expression-controlled)
    for (auto& td : deferred_table_vccs) {
        const auto& refs = td.expr.var_refs();
        int nv = td.expr.num_vars();
        std::vector<const VSource*> vsource_ptrs(nv, nullptr);

        bool table_ok = true;
        for (int i = 0; i < nv; ++i) {
            if (refs[i].kind == asrc::VarKind::BRANCH_CURRENT &&
                !td.vsrc_names[i].empty()) {
                const VSource* vs = nullptr;
                for (const auto& dev : ckt.devices()) {
                    if (auto* v = dynamic_cast<const VSource*>(dev.get())) {
                        if (to_lower(v->name()) == to_lower(td.vsrc_names[i])) {
                            vs = v;
                            break;
                        }
                    }
                }
                if (!vs) {
                    fprintf(stderr, "Warning: Line %d: TABLE VCCS '%s' references unknown voltage source '%s' in I() — skipping\n",
                            td.line_number, td.name.c_str(), td.vsrc_names[i].c_str());
                    table_ok = false;
                    break;
                }
                vsource_ptrs[i] = vs;
            }
        }
        if (!table_ok) continue;

        ckt.add_device(std::make_unique<TableVCCS>(
            td.name, td.np, td.nn,
            std::move(td.expr),
            std::move(td.node_indices),
            std::move(td.node_indices2),
            std::move(vsource_ptrs),
            std::move(td.table_points)));
    }
}

// Helper: case-insensitive prefix test starting at offset `off` in `s`.
// Returns true if s[off..] begins with the (lowercase) literal `prefix`.
static inline bool ci_prefix_at(const std::string& s, size_t off, const char* prefix) {
    size_t i = 0;
    for (; prefix[i] != '\0'; ++i) {
        size_t j = off + i;
        if (j >= s.size()) return false;
        char c = s[j];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != prefix[i]) return false;
    }
    return true;
}

// Helper: the character that would follow a matched prefix is end-of-line or a
// whitespace delimiter (so ".inc" doesn't match ".include" spuriously, etc.).
static inline bool is_word_boundary(const std::string& s, size_t pos) {
    if (pos >= s.size()) return true;
    char c = s[pos];
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
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
            end = line.size();
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

        // Fast path: only lines whose first non-whitespace char is '.' can be a
        // directive handled here (.include/.inc/.lib/.endl). Everything else
        // (the overwhelming majority of library lines: .model cards, element
        // lines, comments) is emitted verbatim without any lowercasing or
        // prefix matching. This avoids a full per-line string allocation for
        // every line of a large included library.
        if (start >= line.size() || line[start] != '.') {
            result << line << '\n';
            continue;
        }

        // ----------------------------------------------------------------
        // .include filename
        // ----------------------------------------------------------------
        bool is_include = false;
        size_t filename_start = 0;
        if (ci_prefix_at(line, start, ".include") && is_word_boundary(line, start + 8)) {
            is_include = true;
            filename_start = start + 8; // position in original line after ".include"
        } else if (ci_prefix_at(line, start, ".inc") && is_word_boundary(line, start + 4)) {
            is_include = true;
            filename_start = start + 4;
        }

        if (is_include) {
            // Skip whitespace after ".include"
            size_t pos = filename_start;
            while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])))
                ++pos;

            if (pos >= line.size()) {
                fprintf(stderr, "Warning: .include directive missing filename — skipping\n");
                result << '\n';
                continue;
            }

            std::string filename = parse_filename_token(line, pos);
            if (filename.empty()) {
                fprintf(stderr, "Warning: .include directive has empty filename — skipping\n");
                result << '\n';
                continue;
            }

            try {
                auto [canonical_path, file_content] = open_lib_file(filename, base_dir, ".include");
                std::string canonical_str = canonical_path.string();
                if (include_stack.count(canonical_str)) {
                    fprintf(stderr, "Warning: .include: circular include detected for file: %s — skipping\n",
                            canonical_str.c_str());
                    continue;
                }

                include_stack.insert(canonical_str);
                std::string included_base_dir = canonical_path.parent_path().string();
                std::string expanded = resolve_includes(file_content, included_base_dir, include_stack);
                include_stack.erase(canonical_str);

                // Auto-close unbalanced .subckt/.ends in included files
                {
                    int sub_depth = 0;
                    std::istringstream scan(expanded);
                    std::string scan_line;
                    while (std::getline(scan, scan_line)) {
                        size_t sp = scan_line.find_first_not_of(" \t");
                        if (sp == std::string::npos || scan_line[sp] != '.')
                            continue;
                        if (ci_prefix_at(scan_line, sp, ".subckt") && is_word_boundary(scan_line, sp + 7))
                            sub_depth++;
                        else if (ci_prefix_at(scan_line, sp, ".ends"))
                            sub_depth = std::max(0, sub_depth - 1);
                    }
                    if (sub_depth > 0) {
                        for (int k = 0; k < sub_depth; ++k) {
                            expanded += "\n.ends";
                        }
                        expanded += '\n';
                    }
                }

                // Strip top-level analysis commands from included files.
                // Library files sometimes contain .dc/.tran/.ac/.op etc.
                // outside .subckt blocks; these are library-author test
                // commands that must not override the main netlist's analysis.
                {
                    static const char* const kAnalysisCmds[] = {
                        ".dc", ".tran", ".ac", ".op",
                        ".noise", ".tf", ".sens",
                        ".pz", ".fourier", ".four", ".meas",
                        ".measure", ".temp", ".step"};
                    std::ostringstream filtered;
                    std::istringstream flines(expanded);
                    std::string fline;
                    int subckt_depth = 0;
                    while (std::getline(flines, fline)) {
                        size_t fp = fline.find_first_not_of(" \t");
                        // Only lines whose first non-blank char is '.' can be a
                        // .subckt/.ends/analysis directive; everything else is
                        // emitted verbatim with no lowercasing.
                        if (fp == std::string::npos || fline[fp] != '.') {
                            filtered << fline << '\n';
                            continue;
                        }
                        if (ci_prefix_at(fline, fp, ".subckt") && is_word_boundary(fline, fp + 7))
                            subckt_depth++;
                        else if (ci_prefix_at(fline, fp, ".ends"))
                            subckt_depth = std::max(0, subckt_depth - 1);
                        if (subckt_depth == 0) {
                            bool is_analysis = false;
                            for (const char* cmd : kAnalysisCmds) {
                                if (ci_prefix_at(fline, fp, cmd) &&
                                    is_word_boundary(fline, fp + std::strlen(cmd)))
                                    { is_analysis = true; break; }
                            }
                            if (is_analysis) continue;
                        }
                        filtered << fline << '\n';
                    }
                    expanded = filtered.str();
                }

                result << expanded;
                if (!expanded.empty() && expanded.back() != '\n') result << '\n';
            } catch (const ParseError& e) {
                fprintf(stderr, "Warning: %s — skipping\n", e.what());
            }
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
        if (ci_prefix_at(line, start, ".lib") && is_word_boundary(line, start + 4)) {
            is_lib = true;
            lib_rest_start = start + 4; // position in original line after ".lib"
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
                try {
                    auto [canonical_path, file_content] = open_lib_file(filename, base_dir, ".lib");
                    std::string canonical_str = canonical_path.string();
                    if (include_stack.count(canonical_str)) {
                        fprintf(stderr, "Warning: .lib: circular include detected for file: %s — skipping\n",
                                canonical_str.c_str());
                    } else {
                        std::string section_content = extract_lib_section(file_content, section);
                        include_stack.insert(canonical_str);
                        std::string lib_base_dir = canonical_path.parent_path().string();
                        std::string expanded = resolve_includes(section_content, lib_base_dir, include_stack);
                        include_stack.erase(canonical_str);

                        result << expanded;
                        if (!expanded.empty() && expanded.back() != '\n') result << '\n';
                    }
                } catch (const ParseError& e) {
                    fprintf(stderr, "Warning: %s — skipping\n", e.what());
                }
            }
            // 0 or 1 tokens after .lib => bare ".lib" or ".lib section_name" delimiter — skip
            continue;
        }

        // ----------------------------------------------------------------
        // .endl — section end delimiter; skip at top level
        // ----------------------------------------------------------------
        bool is_endl = false;
        if (ci_prefix_at(line, start, ".endl") && is_word_boundary(line, start + 5)) {
            is_endl = true;
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
    // Case-insensitive substring search over `content` without building an
    // uppercased copy of the whole (possibly very large) expanded netlist.
    // `needle` must be given in UPPERCASE.
    auto ci_contains = [&content](const char* needle) {
        size_t nlen = std::strlen(needle);
        if (nlen == 0 || content.size() < nlen) return false;
        size_t last = content.size() - nlen;
        for (size_t i = 0; i <= last; ++i) {
            size_t j = 0;
            for (; j < nlen; ++j) {
                char c = content[i + j];
                if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
                if (c != needle[j]) break;
            }
            if (j == nlen) return true;
        }
        return false;
    };

    for (const auto* kw : {"PARAMS:", "AKO:", "VALUE=", "OPTIONAL:", "TEXT:"}) {
        if (ci_contains(kw))
            return SpiceDialect::PSPICE;
    }

    for (const auto* kw : {" VSWITCH ", " ISWITCH ", " RES ", " CAP ", " IND "}) {
        if (ci_contains(kw))
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
    pass1_collect_models_params(state);    // collect raw .model cards (lazy)
    // The API path must hand back a fully-populated model map, so force every
    // collected card to be parsed + AKO-resolved now.
    materialize_all_models(state);

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
