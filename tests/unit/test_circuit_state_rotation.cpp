// Task 7 (deferral 3): verify Circuit::rotate_state rebinds device
// state-pointer caches so devices that call set_state_ptrs once during
// finalize() do not end up aliasing a stale buffer after the ring
// rotates.

#include <gtest/gtest.h>

#include "core/circuit.hpp"
#include "devices/device.hpp"

using namespace neospice;

namespace {

// A stateful device that caches the state pointers it was handed and
// also remembers the base offset.  Exposes the cached pointers so the
// test can check them post-rotation.
class StateProbeDevice : public Device {
public:
    explicit StateProbeDevice(int32_t slots)
        : Device("probe"), slots_(slots) {}

    int32_t state_vars() const override { return slots_; }

    void set_state_ptrs(double* s0, double* s1, double* s2, double* s3,
                        int32_t base) override {
        state0_ = s0;
        state1_ = s1;
        state2_ = s2;
        state3_ = s3;
        base_   = base;
        ++bind_count_;
    }

    // Minimal Device interface — we never stamp or evaluate this device.
    void stamp_pattern(SparsityBuilder&) const override {}
    void assign_offsets(const SparsityPattern&) override {}
    void evaluate(const std::vector<double>&, NumericMatrix&,
                  std::vector<double>&) override {}

    double* state0() const { return state0_; }
    double* state1() const { return state1_; }
    double* state2() const { return state2_; }
    double* state3() const { return state3_; }
    int32_t base() const   { return base_; }
    int     bind_count() const { return bind_count_; }

private:
    int32_t slots_;
    double* state0_ = nullptr;
    double* state1_ = nullptr;
    double* state2_ = nullptr;
    double* state3_ = nullptr;
    int32_t base_   = -1;
    int     bind_count_ = 0;
};

} // namespace

TEST(CircuitStateRotation, RebindsDevicePointersAfterRotate) {
    Circuit ckt;
    auto probe = std::make_unique<StateProbeDevice>(3);
    auto* p    = probe.get();
    ckt.add_device(std::move(probe));
    ckt.finalize();

    // After finalize, exactly one bind and all pointers non-null.
    ASSERT_EQ(p->bind_count(), 1);
    ASSERT_NE(p->state0(), nullptr);
    ASSERT_NE(p->state1(), nullptr);
    ASSERT_NE(p->state2(), nullptr);

    // Seed the state0 ring with a recognisable pattern so we can verify
    // rotate_state actually shuffled the contents.
    for (int i = 0; i < 3; ++i) p->state0()[i] = 10.0 + i;

    ckt.rotate_state();

    // Bind-count should have incremented (rebind_device_states ran).
    EXPECT_EQ(p->bind_count(), 2);

    // Post-rotation semantics: state1 holds the previous state0 values.
    // Whichever concrete buffer the device cached must now reflect that.
    for (int i = 0; i < 3; ++i) {
        EXPECT_DOUBLE_EQ(p->state1()[i], 10.0 + i)
            << "state1 entry " << i
            << " not rebound to the rotated-in state0 contents";
    }

    // All three cached pointers still refer to valid in-range buffers
    // (i.e. they were reset, not left dangling at stale addresses).
    EXPECT_NE(p->state0(), nullptr);
    EXPECT_NE(p->state1(), nullptr);
    EXPECT_NE(p->state2(), nullptr);
}
