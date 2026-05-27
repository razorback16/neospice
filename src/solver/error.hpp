#pragma once

namespace neospice::solver {

enum class SparseError : int {
    OK         = 0,
    SmallPivot = 0,
    NoMemory   = 8,
    Panic      = 101,
    Fatal      = 101,
    ZeroDiag   = 102,
    Singular   = 102,
};

constexpr bool is_fatal(SparseError e) {
    return static_cast<int>(e) >= static_cast<int>(SparseError::Fatal);
}

} // namespace neospice::solver
