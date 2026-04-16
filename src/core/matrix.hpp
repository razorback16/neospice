#pragma once
#include "core/types.hpp"
#include <vector>
#include <utility>
#include <stdexcept>

namespace cudaspice {

/// Compressed Sparse Column data (structure only, no values)
struct CSCData {
    std::vector<int32_t> col_ptr;  // n+1 entries
    std::vector<int32_t> row_idx;  // nnz entries
};

/// Immutable sparsity structure of a square matrix.
class SparsityPattern {
public:
    /// Construct from a sorted, deduplicated list of (row, col) pairs.
    /// entries must be sorted by (col, row) (CSC order).
    SparsityPattern(int32_t n, std::vector<std::pair<int32_t, int32_t>> entries);

    /// Matrix dimension
    int32_t size() const { return n_; }

    /// Number of unique non-zeros
    int32_t nnz() const { return static_cast<int32_t>(entries_.size()); }

    /// Return the index into the values array for (row, col).
    /// Throws std::out_of_range if the position was not registered.
    MatrixOffset offset(int32_t row, int32_t col) const;

    /// Convert to CSC format (col_ptr, row_idx).
    CSCData to_csc() const;

    /// Sorted (row, col) pairs in CSC (col-major) order.
    const std::vector<std::pair<int32_t, int32_t>>& entries() const { return entries_; }

private:
    int32_t n_;
    std::vector<std::pair<int32_t, int32_t>> entries_;  // sorted by (col, row)
};

/// Accumulates sparsity entries before building an immutable SparsityPattern.
class SparsityBuilder {
public:
    explicit SparsityBuilder(int32_t n);

    /// Register a non-zero position. Duplicates are allowed and will be deduped.
    void add(int32_t row, int32_t col);

    /// Deduplicate, sort by (col, row), and return a SparsityPattern.
    SparsityPattern build() const;

private:
    int32_t n_;
    std::vector<std::pair<int32_t, int32_t>> entries_;
};

/// Numeric values associated with a SparsityPattern.
class NumericMatrix {
public:
    explicit NumericMatrix(const SparsityPattern& pattern);

    /// Zero all values.
    void clear();

    /// Accumulate val at the given offset.
    void add(MatrixOffset offset, double val);

    /// Read value at the given offset.
    double value(MatrixOffset offset) const;

    /// Raw pointer to values array (nnz doubles).
    double* data() { return values_.data(); }
    const double* data() const { return values_.data(); }

    /// Number of stored values (equals pattern.nnz()).
    int32_t nnz() const { return static_cast<int32_t>(values_.size()); }

private:
    std::vector<double> values_;
};

}  // namespace cudaspice
