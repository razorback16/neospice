#include "core/linear_solver.hpp"
#include "core/klu_solver.hpp"

namespace neospice {

std::unique_ptr<LinearSolver> create_solver(int32_t n) {
    (void)n;
    return std::make_unique<KLUSolver>();
}

}  // namespace neospice
