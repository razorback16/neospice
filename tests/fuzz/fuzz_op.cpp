// libFuzzer target: parse, then solve the DC operating point.
#include "api/neospice.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string text(reinterpret_cast<const char*>(data), size);
    if (text.find("/dev/") != std::string::npos || text.find("/proc/") != std::string::npos)
        return -1;
    try {
        neospice::Simulator sim;
        auto circuit = sim.parse(text);
        circuit.options.no_throw = true;
        auto op = sim.run_dc(circuit);
    } catch (const std::exception&) {
    }
    return 0;
}
