#pragma once
#include "core/circuit.hpp"
#include <map>
#include <string>
#include <vector>

namespace neospice {

struct SourceSpec {
    double dc = 0.0;
    double ac_mag = 0.0;
    double ac_phase = 0.0;
};

struct PulseSpec {
    double v1 = 0, v2 = 0, td = 0, tr = 0, tf = 0, pw = 0, per = 0;
};

struct SinSpec {
    double vo = 0, va = 0, freq = 0, td = 0, theta = 0, phase = 0;
};

class CircuitBuilder {
public:
    CircuitBuilder& title(const std::string& t);

    CircuitBuilder& resistor(const std::string& name,
                             const std::string& n1, const std::string& n2,
                             double value);
    CircuitBuilder& capacitor(const std::string& name,
                              const std::string& n1, const std::string& n2,
                              double value);
    CircuitBuilder& inductor(const std::string& name,
                             const std::string& n1, const std::string& n2,
                             double value);

    CircuitBuilder& vsource(const std::string& name,
                            const std::string& np, const std::string& nn,
                            const SourceSpec& spec);
    CircuitBuilder& vsource_pulse(const std::string& name,
                                  const std::string& np, const std::string& nn,
                                  const PulseSpec& spec);
    CircuitBuilder& vsource_sin(const std::string& name,
                                const std::string& np, const std::string& nn,
                                const SinSpec& spec);

    CircuitBuilder& isource(const std::string& name,
                            const std::string& np, const std::string& nn,
                            const SourceSpec& spec);

    CircuitBuilder& diode(const std::string& name,
                          const std::string& anode, const std::string& cathode,
                          const std::string& model_name);

    CircuitBuilder& subcircuit(const std::string& name,
                               const std::string& model,
                               const std::vector<std::string>& ports);

    CircuitBuilder& model(const std::string& name, const std::string& type,
                          const std::map<std::string, double>& params);

    CircuitBuilder& include(const std::string& filepath);

    CircuitBuilder& raw_line(const std::string& line);

    Circuit build();

private:
    std::string title_;
    std::vector<std::string> lines_;
    std::string format_value(double v) const;
};

} // namespace neospice
