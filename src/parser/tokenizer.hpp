#pragma once

#include <string>
#include <vector>

namespace cudaspice {

struct TokenizedLine {
    std::vector<std::string> tokens;
    int line_number;
};

// Tokenize a SPICE netlist string into logical lines.
// Handles: title line (skipped), comments (* and $), line continuations (+),
// .end (skipped), and whitespace splitting.
std::vector<TokenizedLine> tokenize(const std::string& netlist);

// Parse a SPICE number with optional suffix (k, meg, u, n, p, f, etc.)
double parse_spice_number(const std::string& str);

} // namespace cudaspice
