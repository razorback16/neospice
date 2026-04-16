#pragma once
#include "core/circuit.hpp"
#include <string>

namespace neospice {

class NetlistParser {
public:
    Circuit parse(const std::string& netlist);
    Circuit parse_file(const std::string& filepath);
};

} // namespace neospice
