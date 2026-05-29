#include "core/solver_iface.hpp"
#include "core/neo_solver.hpp"
#include "core/amd_lu_solver.hpp"
#include <cstdlib>
#include <cstring>

namespace neospice {

namespace {
bool want_amdlu() {
    // NEOSPICE_SOLVER=amdlu  OR  NEOSPICE_FORCE_AMDLU=1
    if (const char* s = std::getenv("NEOSPICE_SOLVER")) {
        if (std::strcmp(s, "amdlu") == 0) return true;
    }
    if (const char* f = std::getenv("NEOSPICE_FORCE_AMDLU")) {
        if (f[0] != '\0' && std::strcmp(f, "0") != 0) return true;
    }
    return false;
}
}  // namespace

std::unique_ptr<ISolver> make_solver() {
    if (want_amdlu())
        return std::make_unique<AmdLuSolver>();
    return std::make_unique<NeoSolver>();
}

}  // namespace neospice
