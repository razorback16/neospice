#pragma once
#include <string>
#include <vector>

namespace neospice {

class Circuit;

struct TopologyDiag {
    enum Type { FLOATING_NODE, VSOURCE_LOOP, ISOURCE_CUTSET, DISCONNECTED };
    enum Severity { WARNING_SEV, ERROR_SEV };

    Type type;
    Severity severity;
    std::string message;
};

std::vector<TopologyDiag> check_topology(const Circuit& ckt);

} // namespace neospice
