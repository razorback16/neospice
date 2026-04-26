#include <nanobind/nanobind.h>

namespace nb = nanobind;

NB_MODULE(_core, m) {
    m.doc() = "neospice: fast SPICE circuit simulator";
}
