#pragma once
#include <cstdint>
#include <optional>
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
    double residual = 0.0;              // final Newton residual norm
    int32_t worst_node_idx = -1;        // node with largest residual
    int gmin_steps = 0;                 // 0 if direct convergence
    int source_steps = 0;               // 0 if no source stepping
    double elapsed_seconds = 0.0;
    std::optional<double> min_timestep; // transient only
    std::vector<std::string> warnings;
};

} // namespace neospice
