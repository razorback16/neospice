#include "core/linear_solver.hpp"
#include "core/klu_solver.hpp"
#include "core/small_solver.hpp"

namespace neospice {

std::unique_ptr<LinearSolver> create_solver(int32_t n) {
    if (n < 200) {
        return std::make_unique<SmallSolver>();
    }
    return std::make_unique<KLUSolver>();
}

}  // namespace neospice
