#include <gtest/gtest.h>

#include "core/circuit.hpp"
#include "core/klu_solver.hpp"
#include "core/newton.hpp"
#include "core/types.hpp"
#include "devices/device.hpp"

#include <vector>

using namespace neospice;

namespace {

// CKTmode bit values we assert against; mirror ngspice cktdefs.h and the
// constants used privately inside newton.cpp.
constexpr int MODEDC_BITS     = 0x70;
constexpr int MODEINITJCT_BIT = 0x200;
constexpr int MODEINITFIX_BIT = 0x400;

// Probe device: trivial single-node linear resistor-to-ground whose only
// job is to record tls_integrator_ctx->mode on every evaluate() call so
// the test can inspect the init-phase flip.
class ModeRecorder : public Device {
public:
    ModeRecorder(std::string name, int32_t node)
        : Device(std::move(name)), node_(node) {}

    void stamp_pattern(SparsityBuilder& builder) const override {
        // Claim one diagonal so Newton converges in one iter (linear).
        builder.add(node_, node_);
    }
    void assign_offsets(const SparsityPattern& pattern) override {
        off_ = pattern.offset(node_, node_);
    }
    void evaluate(const std::vector<double>& /*v*/,
                  NumericMatrix& mat,
                  std::vector<double>& rhs) override {
        modes_.push_back(tls_integrator_ctx ? tls_integrator_ctx->mode : -1);
        mat.add(off_, 1.0);   // 1 S to ground
        rhs[node_] += 1.0;    // inject 1 A -> non-trivial solution so
                              // Newton needs iter 0 (eval+solve) and at
                              // least one more iter to check convergence.
    }

    const std::vector<int>& modes() const { return modes_; }

private:
    int32_t node_;
    MatrixOffset off_ = -1;
    std::vector<int> modes_;
};

} // namespace

TEST(NewtonInitFlip, MODEINITJCTFlipsToFIXAfterIter0) {
    Circuit ckt;
    int32_t na = ckt.node("a");

    auto *rec = new ModeRecorder("XMODE", na);
    ckt.add_device(std::unique_ptr<Device>(rec));
    ckt.finalize();

    // Seed the integrator context the way dc.cpp does — MODEDC | MODEINITJCT.
    ckt.integrator_ctx.mode = MODEDC_BITS | MODEINITJCT_BIT;
    const int saved_mode = ckt.integrator_ctx.mode;

    KLUSolver solver;
    solver.symbolic(ckt.pattern());

    std::vector<double> solution(ckt.num_vars(), 0.0);
    SimOptions opts;
    opts.max_iter = 5;   // plenty — linear converges in 2
    auto result = newton_solve(ckt, solver, solution, opts);

    ASSERT_TRUE(result.converged);
    // Newton drives at least 2 iterations on a linear circuit (initial
    // eval, then convergence check after the first solve).  The device
    // must have been evaluated with MODEINITJCT on iter 0, and with the
    // FIX bit (not JCT) on every iteration after that.
    ASSERT_GE(rec->modes().size(), 2u);
    EXPECT_EQ(rec->modes()[0], MODEDC_BITS | MODEINITJCT_BIT);
    for (std::size_t i = 1; i < rec->modes().size(); ++i) {
        EXPECT_EQ(rec->modes()[i] & MODEINITJCT_BIT, 0)
            << "iter " << i << " still has JCT bit set";
        EXPECT_NE(rec->modes()[i] & MODEINITFIX_BIT, 0)
            << "iter " << i << " missing FIX bit";
        // Base MODEDC bits preserved across the flip.
        EXPECT_EQ(rec->modes()[i] & MODEDC_BITS, MODEDC_BITS)
            << "iter " << i << " lost MODEDC bits";
    }

    // And the Circuit's integrator_ctx.mode must be restored to the
    // original JCT value on return (otherwise a subsequent gmin/source
    // stepping stage would see the wrong bits).
    EXPECT_EQ(ckt.integrator_ctx.mode, saved_mode);
}

TEST(NewtonInitFlip, NoFlipWhenJCTBitAbsent) {
    // If the driver starts with MODEINITFIX (e.g. gmin stepping), newton
    // should NOT flip anything — the FIX bit is already set and there's
    // no JCT bit to clear.
    Circuit ckt;
    int32_t na = ckt.node("a");
    auto *rec = new ModeRecorder("XMODE", na);
    ckt.add_device(std::unique_ptr<Device>(rec));
    ckt.finalize();

    ckt.integrator_ctx.mode = MODEDC_BITS | MODEINITFIX_BIT;
    const int saved_mode = ckt.integrator_ctx.mode;

    KLUSolver solver;
    solver.symbolic(ckt.pattern());
    std::vector<double> solution(ckt.num_vars(), 0.0);
    SimOptions opts;
    opts.max_iter = 5;
    auto result = newton_solve(ckt, solver, solution, opts);

    ASSERT_TRUE(result.converged);
    for (std::size_t i = 0; i < rec->modes().size(); ++i) {
        EXPECT_EQ(rec->modes()[i], saved_mode)
            << "iter " << i << " mode mutated unexpectedly";
    }
    EXPECT_EQ(ckt.integrator_ctx.mode, saved_mode);
}
