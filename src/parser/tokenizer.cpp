#include "parser/tokenizer.hpp"
#include "core/types.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iterator>

namespace neospice {

static std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

// Extract the next whitespace-delimited token from `line` starting at `pos`.
// On return, `pos` points just past the token (at the delimiter or end).
// Returns false if no further token exists.
static inline bool next_ws_token(const std::string& line, size_t& pos, std::string& out) {
    size_t n = line.size();
    while (pos < n) {
        char c = line[pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v')
            ++pos;
        else
            break;
    }
    if (pos >= n) return false;
    size_t begin = pos;
    while (pos < n) {
        char c = line[pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v')
            break;
        ++pos;
    }
    out.assign(line, begin, pos - begin);
    return true;
}

static std::vector<std::string> split_tokens(const std::string& line, size_t pos = 0) {
    std::vector<std::string> tokens;
    tokens.reserve(8);
    std::string tok;
    int brace_depth = 0;
    std::string brace_accum;

    while (next_ws_token(line, pos, tok)) {
        // A '$'-led token starts a comment ONLY when it is the first token of
        // the line. Mid-line, '$' is a valid identifier character (ngspice
        // ngbehavior=psa: newcompat.ps treats '$' as an ordinary id char, see
        // inpcom.c:3194 / inpcompat.c:77). PSpice auto-named nodes like
        // "$N_0002" must survive as node names.
        if (brace_depth == 0 && tok[0] == '$' && tokens.empty()) break;
        // An inline ';' comment may be glued mid-token (e.g. "NOUT;OFF" or
        // "5;note") — ngspice ends the line at ';' anywhere. Honor it at
        // brace-depth 0, tracking braces within the token so a ';' inside a
        // {...} expression is not mistaken for a comment.
        if (brace_depth == 0) {
            int d = 0;
            size_t sc = std::string::npos;
            for (size_t i = 0; i < tok.size(); ++i) {
                if (tok[i] == '{') ++d;
                else if (tok[i] == '}') { if (d > 0) --d; }
                else if (tok[i] == ';' && d == 0) { sc = i; break; }
            }
            if (sc != std::string::npos) {
                tok.erase(sc);
                if (!tok.empty()) tokens.push_back(tok);
                break;
            }
        }

        if (brace_depth > 0) {
            brace_accum += ' ';
            brace_accum += tok;
            for (char c : tok) {
                if (c == '{') ++brace_depth;
                else if (c == '}') --brace_depth;
            }
            if (brace_depth <= 0) {
                brace_depth = 0;
                tokens.push_back(std::move(brace_accum));
                brace_accum.clear();
            }
            continue;
        }

        // Count braces in this token
        int opens = 0, closes = 0;
        for (char c : tok) {
            if (c == '{') ++opens;
            else if (c == '}') ++closes;
        }
        if (opens > closes) {
            brace_depth = opens - closes;
            brace_accum = tok;
            continue;
        }

        tokens.push_back(tok);
    }

    if (!brace_accum.empty()) {
        tokens.push_back(std::move(brace_accum));
    }

    return tokens;
}

// Determine whether the first line should be skipped as a title line.
// In SPICE, the first line of a netlist is the title. However, for single-
// statement inputs (e.g. a single device line), we do not skip it.
// Rule: skip the first line only if there are more than one non-empty,
// non-continuation (not starting with '+') lines in the input.
static bool should_skip_title(const std::string& netlist) {
    std::istringstream stream(netlist);
    std::string line;
    int count = 0;
    while (std::getline(stream, line)) {
        // Trim trailing whitespace/CR
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        if (line.empty()) continue;
        // Skip past leading whitespace: a '+' continuation marker may be
        // indented (matching the main tokenize loop), and such a line is not a
        // standalone statement for title-detection purposes.
        size_t fnw = 0;
        while (fnw < line.size() &&
               std::isspace(static_cast<unsigned char>(line[fnw])))
            ++fnw;
        if (fnw >= line.size() || line[fnw] == '+') continue;
        count++;
        if (count > 1) return true;
    }
    return false;
}

std::vector<TokenizedLine> tokenize(const std::string& netlist) {
    std::vector<TokenizedLine> result;
    std::istringstream stream(netlist);
    std::string raw_line;
    int line_num = 0;
    bool first_line = true;
    bool skip_title = should_skip_title(netlist);

    TokenizedLine current;
    current.line_number = 0;

    while (std::getline(stream, raw_line)) {
        line_num++;

        if (first_line && skip_title) {
            first_line = false;
            continue;
        }
        first_line = false;

        // Trim trailing whitespace and carriage returns
        while (!raw_line.empty() && (raw_line.back() == '\r' || raw_line.back() == ' ' || raw_line.back() == '\t')) {
            raw_line.pop_back();
        }

        if (raw_line.empty()) continue;

        // Find the first non-whitespace character. SPICE allows leading
        // whitespace before the significant token, including before a '+'
        // continuation marker or a '*' comment — ngspice strips it. Vendor
        // libraries frequently indent continuation lines (e.g.
        // "        +Rmax={...}"), so the comment / .end / continuation checks
        // below must look past that leading whitespace, not at raw_line[0].
        size_t fnw = 0;
        while (fnw < raw_line.size() &&
               std::isspace(static_cast<unsigned char>(raw_line[fnw])))
            ++fnw;
        char first_ch = raw_line[fnw];

        if (first_ch == '*') continue;

        // Check for .end directive (case-insensitive) without lowercasing the
        // whole line: only lines starting with '.' can be ".end".
        if (first_ch == '.' && raw_line.size() >= fnw + 4) {
            char c1 = raw_line[fnw + 1], c2 = raw_line[fnw + 2], c3 = raw_line[fnw + 3];
            auto lc = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; };
            if (lc(c1) == 'e' && lc(c2) == 'n' && lc(c3) == 'd' &&
                (raw_line.size() == fnw + 4 ||
                 std::isspace(static_cast<unsigned char>(raw_line[fnw + 4])))) {
                continue;
            }
        }

        // Line continuation: append tokens to current line
        if (first_ch == '+') {
            auto toks = split_tokens(raw_line, fnw + 1);
            current.tokens.insert(current.tokens.end(),
                                  std::make_move_iterator(toks.begin()),
                                  std::make_move_iterator(toks.end()));
            continue;
        }

        // New statement line: flush previous if any
        if (!current.tokens.empty()) {
            result.push_back(std::move(current));
            current = TokenizedLine{};
        }

        current.line_number = line_num;
        current.tokens = split_tokens(raw_line);
    }

    if (!current.tokens.empty()) {
        result.push_back(std::move(current));
    }

    return result;
}

double parse_spice_number(const std::string& str) {
    // Match ngspice's INPevaluate arithmetic, including its observable
    // roundoff. INPevaluate accumulates a decimal mantissa and applies the
    // exponent with pow(10, exponent); strtod can produce an adjacent double.
    const char* p = str.c_str();
    int sign = 1;
    if (*p == '+') {
        ++p;
    } else if (*p == '-') {
        sign = -1;
        ++p;
    }
    if (*p == '\0' || (!std::isdigit(static_cast<unsigned char>(*p)) && *p != '.')) {
        throw ParseError("Invalid number: " + str);
    }

    double mantissa = 0.0;
    int decimal_exponent = 0;
    while (std::isdigit(static_cast<unsigned char>(*p))) {
        mantissa = 10.0 * mantissa + static_cast<double>(*p - '0');
        ++p;
    }
    if (*p == '.') {
        ++p;
        while (std::isdigit(static_cast<unsigned char>(*p))) {
            mantissa = 10.0 * mantissa + static_cast<double>(*p - '0');
            --decimal_exponent;
            ++p;
        }
    }

    int explicit_exponent = 0;
    int exponent_sign = 1;
    if (*p == 'e' || *p == 'E' || *p == 'd' || *p == 'D') {
        ++p;
        if (*p == '+') {
            ++p;
        } else if (*p == '-') {
            exponent_sign = -1;
            ++p;
        }
        while (std::isdigit(static_cast<unsigned char>(*p))) {
            explicit_exponent = 10 * explicit_exponent + (*p - '0');
            ++p;
        }
    }

    switch (std::tolower(static_cast<unsigned char>(*p))) {
    case 't': decimal_exponent += 12; break;
    case 'g': decimal_exponent += 9; break;
    case 'k': decimal_exponent += 3; break;
    case 'u': decimal_exponent -= 6; break;
    case 'n': decimal_exponent -= 9; break;
    case 'p': decimal_exponent -= 12; break;
    case 'f': decimal_exponent -= 15; break;
    case 'a': decimal_exponent -= 18; break;
    case 'm':
        if (std::tolower(static_cast<unsigned char>(p[1])) == 'e' &&
            std::tolower(static_cast<unsigned char>(p[2])) == 'g') {
            decimal_exponent += 6;
        } else if (std::tolower(static_cast<unsigned char>(p[1])) == 'i' &&
                   std::tolower(static_cast<unsigned char>(p[2])) == 'l') {
            decimal_exponent -= 6;
            mantissa *= 25.4;
        } else {
            decimal_exponent -= 3;
        }
        break;
    default: break;
    }

    return static_cast<double>(sign) * mantissa *
           std::pow(10.0, static_cast<double>(decimal_exponent + exponent_sign * explicit_exponent));
}

} // namespace neospice
