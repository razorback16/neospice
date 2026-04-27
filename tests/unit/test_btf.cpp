#include <gtest/gtest.h>
#include "core/btf.hpp"
#include "core/matrix.hpp"
#include <algorithm>

using namespace neospice;

TEST(BTF, SingleBlock) {
    SparsityBuilder sb(3);
    for (int32_t i = 0; i < 3; ++i)
        for (int32_t j = 0; j < 3; ++j)
            sb.add(i, j);
    auto pat = sb.build();
    auto csc = pat.to_csc();
    auto result = btf_decompose(3, csc.col_ptr.data(), csc.row_idx.data());
    EXPECT_EQ(result.nblocks, 1);
    EXPECT_EQ(result.block_ptr.size(), 2u);
    EXPECT_EQ(result.block_ptr[0], 0);
    EXPECT_EQ(result.block_ptr[1], 3);
    std::vector<int32_t> sorted = result.perm;
    std::sort(sorted.begin(), sorted.end());
    EXPECT_EQ(sorted, (std::vector<int32_t>{0, 1, 2}));
    for (int32_t i = 0; i < 3; ++i)
        EXPECT_EQ(result.inv_perm[result.perm[i]], i);
}

TEST(BTF, DiagonalIsNBlocks) {
    int32_t n = 5;
    SparsityBuilder sb(n);
    for (int32_t i = 0; i < n; ++i) sb.add(i, i);
    auto pat = sb.build();
    auto csc = pat.to_csc();
    auto result = btf_decompose(n, csc.col_ptr.data(), csc.row_idx.data());
    EXPECT_EQ(result.nblocks, n);
    for (int32_t i = 0; i <= n; ++i)
        EXPECT_EQ(result.block_ptr[i], i);
}

TEST(BTF, TwoBlocks) {
    SparsityBuilder sb(4);
    // Block A: {0,1} strongly connected. Block B: {2,3} strongly connected.
    sb.add(0, 0); sb.add(0, 1); sb.add(1, 0); sb.add(1, 1);
    sb.add(2, 2); sb.add(2, 3); sb.add(3, 2); sb.add(3, 3);
    // Coupling: row 0, col 2 — edge from block A to block B (upper triangular)
    sb.add(0, 2);
    auto pat = sb.build();
    auto csc = pat.to_csc();
    auto result = btf_decompose(4, csc.col_ptr.data(), csc.row_idx.data());
    EXPECT_EQ(result.nblocks, 2);
    EXPECT_EQ(result.block_ptr[1] - result.block_ptr[0], 2);
    EXPECT_EQ(result.block_ptr[2] - result.block_ptr[1], 2);
    // Block B ({2,3}) should be first (no dependencies), block A ({0,1}) second
    std::vector<int32_t> block0(result.perm.begin() + result.block_ptr[0],
                                 result.perm.begin() + result.block_ptr[1]);
    std::vector<int32_t> block1(result.perm.begin() + result.block_ptr[1],
                                 result.perm.begin() + result.block_ptr[2]);
    std::sort(block0.begin(), block0.end());
    std::sort(block1.begin(), block1.end());
    EXPECT_EQ(block0, (std::vector<int32_t>{2, 3}));
    EXPECT_EQ(block1, (std::vector<int32_t>{0, 1}));
}

TEST(BTF, PermutationIsValid) {
    // Lower-bidiagonal: directed path 0←1←...←9, each node is its own SCC
    int32_t n = 10;
    SparsityBuilder sb(n);
    for (int32_t i = 0; i < n; ++i) {
        sb.add(i, i);
        if (i > 0) sb.add(i, i-1);
    }
    auto pat = sb.build();
    auto csc = pat.to_csc();
    auto result = btf_decompose(n, csc.col_ptr.data(), csc.row_idx.data());
    ASSERT_EQ(static_cast<int32_t>(result.perm.size()), n);
    std::vector<int32_t> sorted = result.perm;
    std::sort(sorted.begin(), sorted.end());
    for (int32_t i = 0; i < n; ++i) EXPECT_EQ(sorted[i], i);
    EXPECT_EQ(result.nblocks, n);
    for (int32_t i = 0; i <= n; ++i)
        EXPECT_EQ(result.block_ptr[i], i);
    for (int32_t i = 0; i < n; ++i)
        EXPECT_EQ(result.inv_perm[result.perm[i]], i);
}
