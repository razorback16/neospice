#include "devices/bsim4v7/bsim4v7_shim.hpp"
#include <cstdarg>

namespace neospice::bsim4v7::Shim {

neospice::MatrixOffset Matrix::make_elt(int row, int col) {
    if (row < 0 || col < 0) return -1;
    builder_.add(row, col);
    // Offset resolution happens later when SparsityPattern is built; caller
    // must call pattern.offset(row, col) themselves, or re-invoke make_elt
    // inside assign_offsets(). For Phase 1a BSIM4setup stores (row, col)
    // pairs as an intermediate; Phase 1b rewrites to direct offset lookup.
    return 0;  // sentinel: "reserved, resolve later"
}

int Ckt::add_internal_node(const char * /*name*/) {
    return CKTinternalNodeCounter++;
}

void report_error(int /*level*/, const char *fmt, ...) {
    std::va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    va_end(ap);
}

} // namespace neospice::bsim4v7::Shim
