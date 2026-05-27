#pragma once
#include <cfloat>
#include <climits>

namespace neospice::solver::config {

constexpr bool EXPANDABLE         = true;
constexpr bool TRANSLATE          = true;
constexpr bool DIAGONAL_PIVOTING  = true;
constexpr bool MODIFIED_MARKOWITZ = false;
constexpr bool MODIFIED_NODAL     = true;
constexpr bool TRANSPOSE          = true;
constexpr bool STRIP              = false;
constexpr bool SCALING            = false;
constexpr bool MULTIPLICATION     = true;
constexpr bool DETERMINANT        = true;
constexpr bool STABILITY          = false;
constexpr bool CONDITION          = false;
constexpr bool DOCUMENTATION      = true;
constexpr bool ARRAY_OFFSET       = true;

constexpr double DEFAULT_THRESHOLD     = 1.0e-3;
constexpr bool   DIAG_PIVOTING_AS_DEFAULT = true;
constexpr int    SPACE_FOR_ELEMENTS    = 6;
constexpr int    SPACE_FOR_FILL_INS    = 4;
constexpr int    ELEMENTS_PER_ALLOCATION = 31;
constexpr int    MINIMUM_ALLOCATED_SIZE = 6;
constexpr double EXPANSION_FACTOR      = 1.5;
constexpr int    MAX_MARKOWITZ_TIES    = 100;
constexpr int    TIES_MULTIPLIER       = 5;
constexpr int    PRINTER_WIDTH         = 80;

constexpr int    DEFAULT_PARTITION     = 0;
constexpr int    DIRECT_PARTITION      = 1;
constexpr int    INDIRECT_PARTITION    = 2;
constexpr int    AUTO_PARTITION        = 3;

constexpr double MACHINE_RESOLUTION   = DBL_EPSILON;
constexpr double LARGEST_REAL         = DBL_MAX;
constexpr double SMALLEST_REAL        = DBL_MIN;
constexpr int    LARGEST_SHORT_INTEGER = SHRT_MAX;
constexpr long   LARGEST_LONG_INTEGER = LONG_MAX;

} // namespace neospice::solver::config
