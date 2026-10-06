#pragma once
#include "core/newton.hpp"
#include "core/reuse_statistics.hpp"
#include "core/neo_solver.hpp"
#include <memory>
#include <string>

namespace neospice {
// Owned by one finalized Circuit. Topology cannot change after finalization.
struct AnalysisCache {
    std::unique_ptr<ISolver> dc_solver;
    std::unique_ptr<NewtonWorkspace> dc_workspace;
    std::unique_ptr<NeoSolver> ac_solver;
    std::string dc_policy;
    bool dc_ready = false;
    bool ac_ready = false;
    ReuseStatistics statistics;
};
} // namespace neospice
