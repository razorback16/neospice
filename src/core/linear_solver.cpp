#include "core/linear_solver.hpp"
#include "core/neo_solver.hpp"

namespace neospice {

std::unique_ptr<LinearSolver> create_solver(int32_t n) {
    (void)n;
    return std::make_unique<NeoSolver>();
}

}  // namespace neospice
