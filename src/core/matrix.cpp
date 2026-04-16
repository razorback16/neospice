#include "core/matrix.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>

namespace cudaspice {

// ---------------------------------------------------------------------------
// SparsityPattern
// ---------------------------------------------------------------------------

SparsityPattern::SparsityPattern(int32_t n,
                                 std::vector<std::pair<int32_t, int32_t>> entries)
    : n_(n), entries_(std::move(entries)) {}

MatrixOffset SparsityPattern::offset(int32_t row, int32_t col) const {
    // entries_ is sorted by (col, row) — binary search on (col, row)
    auto target = std::make_pair(col, row);
    // We need to search by (col, row) but entries_ stores (row, col)
    // So we search for the entry where entry.second == col and entry.first == row
    // Equivalently: find entry == {row, col} in the (col, row)-sorted list.
    // Since we sorted by (col, row), we can binary-search treating each entry
    // as the key (col, row) = (entry.second, entry.first).

    // Use lower_bound with a custom comparator over the index range
    auto it = std::lower_bound(
        entries_.begin(), entries_.end(),
        std::make_pair(row, col),
        [](const std::pair<int32_t, int32_t>& a,
           const std::pair<int32_t, int32_t>& b) {
            // CSC order: primary key is col (a.second), secondary is row (a.first)
            if (a.second != b.second) return a.second < b.second;
            return a.first < b.first;
        });

    if (it == entries_.end() || it->first != row || it->second != col) {
        throw std::out_of_range(
            "SparsityPattern::offset: position (" + std::to_string(row) +
            ", " + std::to_string(col) + ") not in pattern");
    }
    return static_cast<MatrixOffset>(it - entries_.begin());
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
    values_[offset] += val;
}

double NumericMatrix::value(MatrixOffset offset) const {
    return values_[offset];
}

}  // namespace cudaspice
