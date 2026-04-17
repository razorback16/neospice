#include <gtest/gtest.h>
#include "devices/bsim4v7/bsim4v7_shim.hpp"
#include "core/matrix.hpp"

using namespace neospice;
using namespace neospice::bsim4v7;

TEST(Bsim4ShimMatrix, TwoPhaseResolution) {
    SparsityBuilder b(4);
    Shim::Matrix mat(b);

    // Phase 1: reservations. IDs are sequential, starting at 0.
    auto id_00 = mat.make_elt(0, 0);
    auto id_11 = mat.make_elt(1, 1);
    auto id_01 = mat.make_elt(0, 1);
    auto id_gd = mat.make_elt(-1, 2); // ground row — sentinel
    EXPECT_EQ(0, id_00);
    EXPECT_EQ(1, id_11);
    EXPECT_EQ(2, id_01);
    EXPECT_EQ(-1, id_gd);

    // Phase 2: resolve against finalized pattern.
    SparsityPattern pat = b.build();
    auto offsets = mat.resolve_offsets(pat);
    ASSERT_EQ(4u, offsets.size());
    EXPECT_GE(offsets[0], 0);
    EXPECT_GE(offsets[1], 0);
    EXPECT_GE(offsets[2], 0);
    EXPECT_EQ(-1, offsets[3]);     // ground stays -1

    EXPECT_EQ(offsets[0], pat.offset(0, 0));
    EXPECT_EQ(offsets[1], pat.offset(1, 1));
    EXPECT_EQ(offsets[2], pat.offset(0, 1));
}
