#include <gtest/gtest.h>
#include "core/matrix.hpp"

using namespace cudaspice;

// ---------------------------------------------------------------------------
// SparsityBuilder tests
// ---------------------------------------------------------------------------

TEST(SparsityBuilder, AddsEntries) {
    SparsityBuilder sb(2);
    sb.add(0, 0);
    sb.add(0, 1);
    sb.add(1, 0);
    sb.add(1, 1);
    SparsityPattern pat = sb.build();
    EXPECT_EQ(pat.size(), 2);
    EXPECT_EQ(pat.nnz(), 4);
}

TEST(SparsityBuilder, DeduplicatesEntries) {
    SparsityBuilder sb(3);
    sb.add(0, 0);
    sb.add(0, 0);  // duplicate
    sb.add(1, 2);
    sb.add(1, 2);  // duplicate
    SparsityPattern pat = sb.build();
    EXPECT_EQ(pat.nnz(), 2);
}

// ---------------------------------------------------------------------------
// SparsityPattern tests
// ---------------------------------------------------------------------------

TEST(SparsityPattern, ReturnsOffset) {
    SparsityBuilder sb(3);
    sb.add(0, 0);
    sb.add(1, 1);
    sb.add(2, 2);
    SparsityPattern pat = sb.build();

    // All offsets must be unique and in [0, nnz)
    MatrixOffset o0 = pat.offset(0, 0);
    MatrixOffset o1 = pat.offset(1, 1);
    MatrixOffset o2 = pat.offset(2, 2);

    EXPECT_GE(o0, 0); EXPECT_LT(o0, pat.nnz());
    EXPECT_GE(o1, 0); EXPECT_LT(o1, pat.nnz());
    EXPECT_GE(o2, 0); EXPECT_LT(o2, pat.nnz());

    EXPECT_NE(o0, o1);
    EXPECT_NE(o1, o2);
    EXPECT_NE(o0, o2);
}

TEST(SparsityPattern, ThrowsForMissingOffset) {
    SparsityBuilder sb(2);
    sb.add(0, 0);
    SparsityPattern pat = sb.build();
    EXPECT_THROW(pat.offset(1, 1), std::out_of_range);
}

TEST(SparsityPattern, CSCConversion) {
    // Full 2x2 matrix: (0,0),(1,0),(0,1),(1,1)
    SparsityBuilder sb(2);
    sb.add(0, 0);
    sb.add(1, 0);
    sb.add(0, 1);
    sb.add(1, 1);
    SparsityPattern pat = sb.build();
    CSCData csc = pat.to_csc();

    // col_ptr has n+1 = 3 entries
    ASSERT_EQ(static_cast<int>(csc.col_ptr.size()), 3);
    // col 0 has 2 entries, col 1 has 2 entries
    EXPECT_EQ(csc.col_ptr[0], 0);
    EXPECT_EQ(csc.col_ptr[1], 2);
    EXPECT_EQ(csc.col_ptr[2], 4);

    // row_idx has nnz = 4 entries, in CSC order: col0=[0,1], col1=[0,1]
    ASSERT_EQ(static_cast<int>(csc.row_idx.size()), 4);
    EXPECT_EQ(csc.row_idx[0], 0);
    EXPECT_EQ(csc.row_idx[1], 1);
    EXPECT_EQ(csc.row_idx[2], 0);
    EXPECT_EQ(csc.row_idx[3], 1);
}

// ---------------------------------------------------------------------------
// NumericMatrix tests
// ---------------------------------------------------------------------------

TEST(NumericMatrix, StampAndClear) {
    SparsityBuilder sb(2);
    sb.add(0, 0);
    sb.add(1, 1);
    SparsityPattern pat = sb.build();
    NumericMatrix mat(pat);

    MatrixOffset o0 = pat.offset(0, 0);
    MatrixOffset o1 = pat.offset(1, 1);

    mat.add(o0, 3.14);
    mat.add(o1, 2.72);
    mat.add(o0, 1.0);  // accumulate

    EXPECT_DOUBLE_EQ(mat.value(o0), 4.14);
    EXPECT_DOUBLE_EQ(mat.value(o1), 2.72);

    mat.clear();
    EXPECT_DOUBLE_EQ(mat.value(o0), 0.0);
    EXPECT_DOUBLE_EQ(mat.value(o1), 0.0);
}

TEST(NumericMatrix, NnzMatchesPattern) {
    SparsityBuilder sb(3);
    sb.add(0, 1);
    sb.add(2, 0);
    sb.add(1, 2);
    SparsityPattern pat = sb.build();
    NumericMatrix mat(pat);
    EXPECT_EQ(mat.nnz(), pat.nnz());
    EXPECT_EQ(mat.nnz(), 3);
}

TEST(NumericMatrix, DataPointer) {
    SparsityBuilder sb(2);
    sb.add(0, 0);
    SparsityPattern pat = sb.build();
    NumericMatrix mat(pat);
    MatrixOffset o = pat.offset(0, 0);
    mat.add(o, 5.0);
    EXPECT_DOUBLE_EQ(mat.data()[o], 5.0);
    // const version
    const NumericMatrix& cmat = mat;
    EXPECT_DOUBLE_EQ(cmat.data()[o], 5.0);
}
