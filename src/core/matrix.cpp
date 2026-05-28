#include "core/matrix.hpp"
#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <string>

namespace neospice {

// ---------------------------------------------------------------------------
// SparsityPattern
// ---------------------------------------------------------------------------

SparsityPattern::SparsityPattern(int32_t n,
                                 std::vector<std::pair<int32_t, int32_t>> entries)
    : n_(n), entries_(std::move(entries)) {}

MatrixOffset SparsityPattern::find_offset(int32_t row, int32_t col) const {
    // entries_ is sorted by (col, row).
    auto it = std::lower_bound(
        entries_.begin(), entries_.end(),
        std::make_pair(row, col),
        [](const std::pair<int32_t, int32_t>& a,
           const std::pair<int32_t, int32_t>& b) {
            if (a.second != b.second) return a.second < b.second;
            return a.first < b.first;
        });

    if (it == entries_.end() || it->first != row || it->second != col)
        return -1;
    return static_cast<MatrixOffset>(it - entries_.begin());
}

MatrixOffset SparsityPattern::offset(int32_t row, int32_t col) const {
    MatrixOffset off = find_offset(row, col);
    if (off < 0) {
        throw std::out_of_range(
            "SparsityPattern::offset: position (" + std::to_string(row) +
            ", " + std::to_string(col) + ") not in pattern");
    }
    return off;
}

CSCData SparsityPattern::to_csc() const {
    CSCData csc;
    csc.col_ptr.assign(n_ + 1, 0);
    csc.row_idx.reserve(entries_.size());

    // Count entries per column
    for (const auto& [r, c] : entries_) {
        csc.col_ptr[c + 1]++;
    }
    // Prefix sum
    for (int32_t j = 0; j < n_; ++j) {
        csc.col_ptr[j + 1] += csc.col_ptr[j];
    }
    // Fill row indices (entries already in CSC order so we just append)
    for (const auto& [r, c] : entries_) {
        csc.row_idx.push_back(r);
    }
    return csc;
}

// ---------------------------------------------------------------------------
// SparsityBuilder
// ---------------------------------------------------------------------------

SparsityBuilder::SparsityBuilder(int32_t n) : n_(n) {}

void SparsityBuilder::add(int32_t row, int32_t col) {
    entries_.emplace_back(row, col);
}

bool SparsityBuilder::has_diagonal(int32_t i) const {
    for (const auto& [r, c] : entries_)
        if (r == i && c == i) return true;
    return false;
}

SparsityPattern SparsityBuilder::build() const {
    auto sorted = entries_;
    // Sort by (col, row) for CSC layout
    std::sort(sorted.begin(), sorted.end(),
              [](const std::pair<int32_t, int32_t>& a,
                 const std::pair<int32_t, int32_t>& b) {
                  if (a.second != b.second) return a.second < b.second;
                  return a.first < b.first;
              });
    // Deduplicate
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    return SparsityPattern(n_, std::move(sorted));
}

// ---------------------------------------------------------------------------
// NumericMatrix
// ---------------------------------------------------------------------------

NumericMatrix::NumericMatrix(const SparsityPattern& pattern)
    : values_(pattern.nnz(), 0.0) {}

void NumericMatrix::clear() {
    std::fill(values_.begin(), values_.end(), 0.0);
}

void NumericMatrix::add(MatrixOffset offset, double val) {
    // Sentinel-only semantics — NOT error swallowing:
    //
    //   offset == -1  : canonical ground sentinel, NO-OP silently.
    //   offset <  -1  : debug-assert fires (garbage offset from a broken
    //                   stamp path).  Release builds still tolerate the
    //                   bogus value as a no-op to stay memory-safe, but
    //                   the assert catches the bug during development.
    //   offset >=  0  : normal stamp into the values_ array.
    //
    // Devices produce -1 whenever a stamp target maps to ground — the
    // translated UCB BSIM4v7 load and Device::add_if_valid() in
    // devices/device.hpp both rely on this so they don't have to guard
    // every mat.add() call.  If you find yourself wanting to "allow" any
    // other negative value, you have a sparsity-offset resolution bug —
    // do not remove the assert.
    assert(offset >= -1 &&
           "NumericMatrix::add: offset must be >= -1; -1 is the ground sentinel");
    if (offset < 0) return;  // sentinel: ground node — no-op
    values_[offset] += val;
}

double NumericMatrix::value(MatrixOffset offset) const {
    return values_[offset];
}

}  // namespace neospice
