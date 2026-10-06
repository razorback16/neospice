#include "core/pz.hpp"
#include <stdexcept>
namespace neospice {
PZResult solve_pz(Circuit&, const std::string&, const std::string&,
                  const std::string&, const std::string&, PZTransferType, PZType) {
    throw std::runtime_error("Pole-zero analysis requires the native LAPACK build");
}
}
