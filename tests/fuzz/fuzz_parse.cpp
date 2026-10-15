// libFuzzer target: netlist text -> NetlistParser::parse().
// Parse errors are expected and reported as std::exception; anything else
// (sanitizer report, crash, non-standard exception, hang, OOM) is a finding.
#include "parser/netlist_parser.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string text(reinterpret_cast<const char*>(data), size);
    // Keep the target hermetic: never read device or process files.
    if (text.find("/dev/") != std::string::npos || text.find("/proc/") != std::string::npos)
        return -1;
    try {
        neospice::NetlistParser parser;
        auto circuit = parser.parse(text);
    } catch (const std::exception&) {
    }
    return 0;
}
