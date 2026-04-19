#pragma once
#include "core/circuit.hpp"
#include "parser/subcircuit.hpp"
#include <set>
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

    // Recursively resolve .include directives in a netlist string.
    // base_dir: directory of the file containing this netlist (for relative paths)
    // include_stack: set of canonical paths already being processed (for circular detection)
    std::string resolve_includes(const std::string& content,
                                 const std::string& base_dir,
                                 std::set<std::string>& include_stack);
};

} // namespace neospice
