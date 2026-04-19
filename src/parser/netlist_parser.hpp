#pragma once
#include "core/circuit.hpp"
#include "parser/subcircuit.hpp"
#include <string>
#include <unordered_map>

namespace neospice {

class NetlistParser {
public:
    Circuit parse(const std::string& netlist);
    Circuit parse_file(const std::string& filepath);

    const std::unordered_map<std::string, SubcircuitDef>& subcircuit_defs() const {
        return subcircuit_defs_;
    }

private:
    std::unordered_map<std::string, SubcircuitDef> subcircuit_defs_;
};

} // namespace neospice
