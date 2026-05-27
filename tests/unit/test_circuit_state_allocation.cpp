#include <gtest/gtest.h>
#include "core/circuit.hpp"
#include "devices/device.hpp"

using namespace neospice;

namespace {
// Dummy device with non-zero state_vars() to drive allocation.
struct FakeStateDev : public Device {
    FakeStateDev(std::string n, int32_t nv) : Device(std::move(n)), nv_(nv) {}
    void stamp_pattern(SparsityBuilder&) const override {}
    void assign_offsets(const SparsityPattern&) override {}
    void evaluate(const std::vector<double>&, NumericMatrix&, std::vector<double>&) override {}
    int32_t state_vars() const override { return nv_; }
    void set_state_ptrs(double* s0, double* s1, double* s2, double* s3, int32_t base) override {
        bound0_ = s0; bound1_ = s1; bound2_ = s2; bound3_ = s3; base_ = base;
    }
    int32_t nv_;
    double *bound0_ = nullptr, *bound1_ = nullptr, *bound2_ = nullptr, *bound3_ = nullptr;
    int32_t base_ = -1;
};
}

TEST(CircuitState, AllocatesRingAndBinds) {
    Circuit ckt;
    ckt.node("a"); ckt.node("b");
    auto *fa = new FakeStateDev("X1", 29);
    auto *fb = new FakeStateDev("X2", 12);
    ckt.add_device(std::unique_ptr<Device>(fa));
    ckt.add_device(std::unique_ptr<Device>(fb));
    ckt.finalize();

    EXPECT_EQ(41, ckt.num_states());
    EXPECT_NE(nullptr, fa->bound0_);
    EXPECT_NE(nullptr, fb->bound0_);
    EXPECT_EQ(0,  fa->base_);
    EXPECT_EQ(29, fb->base_);
    // state0/1/2 are distinct buffers
    EXPECT_NE(fa->bound0_, fa->bound1_);
    EXPECT_NE(fa->bound1_, fa->bound2_);
}
