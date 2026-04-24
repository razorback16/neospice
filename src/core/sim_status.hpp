#pragma once
#include <string>
#include <vector>

namespace neospice {

enum class ConvergenceMethod {
    DIRECT,
    GMIN_STEPPING,
    SOURCE_STEPPING,
    PSEUDO_TRANSIENT
};

inline const char* convergence_method_name(ConvergenceMethod m) {
    switch (m) {
    case ConvergenceMethod::DIRECT:             return "direct";
    case ConvergenceMethod::GMIN_STEPPING:      return "gmin-stepping";
    case ConvergenceMethod::SOURCE_STEPPING:    return "source-stepping";
    case ConvergenceMethod::PSEUDO_TRANSIENT:   return "pseudo-transient";
    default:                                     return "unknown";
    }
}

struct SimStatus {
    bool converged = true;
    int iterations = 0;
    ConvergenceMethod convergence_method = ConvergenceMethod::DIRECT;
    std::vector<std::string> warnings;
    double elapsed_seconds = 0.0;
};

} // namespace neospice
