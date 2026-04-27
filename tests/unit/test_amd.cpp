#include <gtest/gtest.h>
#include <algorithm>
#include "core/amd.hpp"
#include "core/matrix.hpp"

using namespace neospice;

TEST(AMD, IdentityOnDiagonal) {
    SparsityBuilder sb(3);
    sb.add(0, 0); sb.add(1, 1); sb.add(2, 2);
    auto pat = sb.build();
    auto csc = pat.to_csc();
    auto perm = amd_ordering(3, csc.col_ptr.data(), csc.row_idx.data());
    ASSERT_EQ(perm.size(), 3u);
    std::vector<int32_t> sorted = perm;
    std::sort(sorted.begin(), sorted.end());
    EXPECT_EQ(sorted, (std::vector<int32_t>{0, 1, 2}));
}

TEST(AMD, TridiagonalPermutationIsValid) {
    int32_t n = 10;
    SparsityBuilder sb(n);
    for (int32_t i = 0; i < n; ++i) {
        sb.add(i, i);
        if (i > 0) { sb.add(i, i-1); sb.add(i-1, i); }
    }
    auto pat = sb.build();
    auto csc = pat.to_csc();
    auto perm = amd_ordering(n, csc.col_ptr.data(), csc.row_idx.data());
    ASSERT_EQ(static_cast<int32_t>(perm.size()), n);
    std::vector<int32_t> sorted = perm;
    std::sort(sorted.begin(), sorted.end());
    for (int32_t i = 0; i < n; ++i) EXPECT_EQ(sorted[i], i);
}

TEST(AMD, ArrowheadReducesFill) {
    int32_t n = 20;
    SparsityBuilder sb(n);
    for (int32_t i = 0; i < n; ++i) {
        sb.add(i, i);
        if (i > 0) { sb.add(0, i); sb.add(i, 0); }
    }
    auto pat = sb.build();
    auto csc = pat.to_csc();
    auto perm = amd_ordering(n, csc.col_ptr.data(), csc.row_idx.data());
    int32_t pos_of_zero = -1;
    for (int32_t i = 0; i < n; ++i)
        if (perm[i] == 0) { pos_of_zero = i; break; }
    EXPECT_GE(pos_of_zero, n - 2) << "High-degree node should be ordered last";
}

TEST(AMD, MatchesSuiteSparseOnBanded) {
    int32_t n = 50;
    SparsityBuilder sb(n);
    for (int32_t i = 0; i < n; ++i) {
        for (int32_t d = -3; d <= 3; ++d) {
            int32_t j = i + d;
            if (j >= 0 && j < n) sb.add(i, j);
        }
    }
    auto pat = sb.build();
    auto csc = pat.to_csc();
    auto perm = amd_ordering(n, csc.col_ptr.data(), csc.row_idx.data());
    ASSERT_EQ(static_cast<int32_t>(perm.size()), n);
    std::vector<int32_t> sorted = perm;
    std::sort(sorted.begin(), sorted.end());
    for (int32_t i = 0; i < n; ++i) EXPECT_EQ(sorted[i], i);
}
