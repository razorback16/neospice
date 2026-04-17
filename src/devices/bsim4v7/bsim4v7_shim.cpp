#include "devices/bsim4v7/bsim4v7_shim.hpp"
#include <cstdarg>

namespace neospice::bsim4v7::Shim {

neospice::MatrixOffset Matrix::make_elt(int row, int col) {
    if (row < 0 || col < 0) {
        journal_.emplace_back(-1, -1);
        return -1;
    }
    builder_.add(row, col);
    neospice::MatrixOffset id = static_cast<neospice::MatrixOffset>(journal_.size());
    journal_.emplace_back(row, col);
    return id;
}

std::vector<neospice::MatrixOffset>
Matrix::resolve_offsets(const neospice::SparsityPattern &pat) const {
    std::vector<neospice::MatrixOffset> out;
    out.reserve(journal_.size());
    for (auto &[r, c] : journal_) {
        if (r < 0 || c < 0) out.push_back(-1);
        else out.push_back(pat.offset(r, c));
    }
    return out;
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
