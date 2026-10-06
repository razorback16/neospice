#pragma once
#include <cstddef>
namespace neospice {
struct ReuseStatistics {
    std::size_t dc_symbolic_analyses = 0, ac_symbolic_analyses = 0;
    std::size_t dc_runs = 0, ac_runs = 0;
};

}
