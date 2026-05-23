#include "parser/tokenizer.hpp"
#include "core/types.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace neospice {

static std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

static std::vector<std::string> split_tokens(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string tok;
    int brace_depth = 0;
    std::string brace_accum;

    while (iss >> tok) {
        if (brace_depth == 0 && tok[0] == '$') break;
        if (brace_depth == 0 && tok[0] == ';') break;

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
        if (line[0] == '+') continue;
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
        if (raw_line[0] == '*') continue;

        // Check for .end directive
        std::string lower_line = to_lower(raw_line);
        if (lower_line.substr(0, 4) == ".end" &&
            (raw_line.size() == 4 || std::isspace(static_cast<unsigned char>(raw_line[4])))) {
            continue;
        }

        // Line continuation: append tokens to current line
        if (raw_line[0] == '+') {
            auto toks = split_tokens(raw_line.substr(1));
            current.tokens.insert(current.tokens.end(), toks.begin(), toks.end());
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
    std::string s = str;
    char* end = nullptr;
    double val = std::strtod(s.c_str(), &end);

    if (end == s.c_str()) {
        throw ParseError("Invalid number: " + str);
    }

    std::string suffix(end);
    std::string lsuffix = to_lower(suffix);

    if (lsuffix.empty()) return val;
    if (lsuffix[0] == 't' || lsuffix.substr(0, 4) == "tera") return val * 1e12;
    if (lsuffix[0] == 'g' || lsuffix.substr(0, 4) == "giga") return val * 1e9;
    if (lsuffix.substr(0, 3) == "meg") return val * 1e6;
    if (lsuffix[0] == 'k') return val * 1e3;
    if (lsuffix.substr(0, 3) == "mil") return val * 25.4e-6;
    if (lsuffix[0] == 'm' && lsuffix.substr(0, 3) != "meg") return val * 1e-3;
    if (lsuffix[0] == 'u') return val * 1e-6;
    if (lsuffix[0] == 'n') return val * 1e-9;
    if (lsuffix[0] == 'p') return val * 1e-12;
    if (lsuffix[0] == 'f') return val * 1e-15;
    if (lsuffix[0] == 'a') return val * 1e-18;

    return val;
}

} // namespace neospice
