#pragma once
#include "parser/tokenizer.hpp"
#include <string>
#include <vector>
#include <utility>

namespace neospice {

struct SubcircuitDef {
    std::string name;
    std::vector<std::string> ports;          // port names in order
    std::vector<std::pair<std::string, std::string>> default_params;  // (name, expr)
    std::vector<TokenizedLine> body;         // raw lines between .subckt and .ends
};

} // namespace neospice
