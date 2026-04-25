#include "api/circuit_builder.hpp"
#include "parser/netlist_parser.hpp"
#include <sstream>
#include <cmath>
#include <iomanip>

namespace neospice {

CircuitBuilder& CircuitBuilder::title(const std::string& t) {
    title_ = t;
    return *this;
}

std::string CircuitBuilder::format_value(double v) const {
    std::ostringstream oss;
    oss << std::setprecision(15) << v;
    return oss.str();
}

CircuitBuilder& CircuitBuilder::resistor(const std::string& name,
                                         const std::string& n1, const std::string& n2,
                                         double value) {
    lines_.push_back(name + " " + n1 + " " + n2 + " " + format_value(value));
    return *this;
}

CircuitBuilder& CircuitBuilder::capacitor(const std::string& name,
                                          const std::string& n1, const std::string& n2,
                                          double value) {
    lines_.push_back(name + " " + n1 + " " + n2 + " " + format_value(value));
    return *this;
}

CircuitBuilder& CircuitBuilder::inductor(const std::string& name,
                                         const std::string& n1, const std::string& n2,
                                         double value) {
    lines_.push_back(name + " " + n1 + " " + n2 + " " + format_value(value));
    return *this;
}

CircuitBuilder& CircuitBuilder::vsource(const std::string& name,
                                        const std::string& np, const std::string& nn,
                                        const SourceSpec& spec) {
    std::string line = name + " " + np + " " + nn;
    if (spec.dc != 0.0) line += " DC " + format_value(spec.dc);
    if (spec.ac_mag != 0.0) {
        line += " AC " + format_value(spec.ac_mag);
        if (spec.ac_phase != 0.0)
            line += " " + format_value(spec.ac_phase);
    }
    lines_.push_back(line);
    return *this;
}

CircuitBuilder& CircuitBuilder::vsource_pulse(const std::string& name,
                                              const std::string& np, const std::string& nn,
                                              const PulseSpec& spec) {
    std::string line = name + " " + np + " " + nn + " PULSE("
        + format_value(spec.v1) + " " + format_value(spec.v2) + " "
        + format_value(spec.td) + " " + format_value(spec.tr) + " "
        + format_value(spec.tf) + " " + format_value(spec.pw) + " "
        + format_value(spec.per) + ")";
    lines_.push_back(line);
    return *this;
}

CircuitBuilder& CircuitBuilder::vsource_sin(const std::string& name,
                                            const std::string& np, const std::string& nn,
                                            const SinSpec& spec) {
    std::string line = name + " " + np + " " + nn + " SIN("
        + format_value(spec.vo) + " " + format_value(spec.va) + " "
        + format_value(spec.freq) + " " + format_value(spec.td) + " "
        + format_value(spec.theta) + " " + format_value(spec.phase) + ")";
    lines_.push_back(line);
    return *this;
}

CircuitBuilder& CircuitBuilder::isource(const std::string& name,
                                        const std::string& np, const std::string& nn,
                                        const SourceSpec& spec) {
    std::string line = name + " " + np + " " + nn;
    if (spec.dc != 0.0) line += " DC " + format_value(spec.dc);
    if (spec.ac_mag != 0.0) {
        line += " AC " + format_value(spec.ac_mag);
        if (spec.ac_phase != 0.0)
            line += " " + format_value(spec.ac_phase);
    }
    lines_.push_back(line);
    return *this;
}

CircuitBuilder& CircuitBuilder::diode(const std::string& name,
                                      const std::string& anode, const std::string& cathode,
                                      const std::string& model_name) {
    lines_.push_back(name + " " + anode + " " + cathode + " " + model_name);
    return *this;
}

CircuitBuilder& CircuitBuilder::subcircuit(const std::string& name,
                                           const std::string& model,
                                           const std::vector<std::string>& ports) {
    std::string line = name;
    for (const auto& p : ports) line += " " + p;
    line += " " + model;
    lines_.push_back(line);
    return *this;
}

CircuitBuilder& CircuitBuilder::model(const std::string& name, const std::string& type,
                                      const std::map<std::string, double>& params) {
    std::string line = ".model " + name + " " + type;
    for (const auto& [k, v] : params)
        line += " " + k + "=" + format_value(v);
    lines_.push_back(line);
    return *this;
}

CircuitBuilder& CircuitBuilder::include(const std::string& filepath) {
    lines_.push_back(".include " + filepath);
    return *this;
}

CircuitBuilder& CircuitBuilder::raw_line(const std::string& line) {
    lines_.push_back(line);
    return *this;
}

Circuit CircuitBuilder::build() {
    std::string netlist;
    netlist += title_.empty() ? "CircuitBuilder" : title_;
    netlist += "\n";
    for (const auto& line : lines_)
        netlist += line + "\n";
    netlist += ".end\n";

    NetlistParser parser;
    return parser.parse(netlist);
}

} // namespace neospice
