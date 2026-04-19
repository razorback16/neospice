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
constexpr int MODEDCOP_BIT       = 0x10;
constexpr int MODEINITFLOAT_BIT  = 0x100;
constexpr int MODEINITJCT_BIT    = 0x200;
constexpr int MODEINITFIX_BIT    = 0x400;
constexpr int MODEINITTRAN_BIT   = 0x1000;
constexpr int MODEINITPRED_BIT   = 0x2000;
constexpr int MODETRAN_BIT       = 0x1;

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
    // Mirrors ngspice NIiter init-flag cascade for DC analysis:
    //   iter 0: device sees MODEINITJCT (junction guess)
    //   iter 1: device sees MODEINITFIX (reads CKTrhsOld)
    //   iter 2: device sees MODEINITFLOAT (converged under FIX -> FLOAT)
    //   iter 2 converges -> return
    Circuit ckt;
    int32_t na = ckt.node("a");

    auto *rec = new ModeRecorder("XMODE", na);
    ckt.add_device(std::unique_ptr<Device>(rec));
    ckt.finalize();

    // Seed the integrator context the way dc.cpp does — MODEDCOP | MODEINITJCT.
    ckt.integrator_ctx.mode = MODEDCOP_BIT | MODEINITJCT_BIT;
    const int saved_mode = ckt.integrator_ctx.mode;

    KLUSolver solver;
    solver.symbolic(ckt.pattern());

    std::vector<double> solution(ckt.num_vars(), 0.0);
    SimOptions opts;
    opts.max_iter = 10;   // plenty — linear converges quickly
    auto result = newton_solve(ckt, solver, solution, opts);

    ASSERT_TRUE(result.converged);

    // The full cascade: JCT (iter 0) -> FIX (iter 1) -> FLOAT (iter 2).
    // Newton must run at least 3 iterations for a DC-JCT start.
    ASSERT_GE(rec->modes().size(), 3u);

    // iter 0: device sees JCT
    EXPECT_EQ(rec->modes()[0], MODEDCOP_BIT | MODEINITJCT_BIT);

    // iter 1: device sees FIX (JCT -> FIX flip after iter 0)
    EXPECT_EQ(rec->modes()[1] & MODEINITJCT_BIT, 0)
        << "iter 1 still has JCT bit set";
    EXPECT_NE(rec->modes()[1] & MODEINITFIX_BIT, 0)
        << "iter 1 missing FIX bit";
    EXPECT_NE(rec->modes()[1] & MODEDCOP_BIT, 0)
        << "iter 1 lost MODEDCOP bit";

    // iter 2+: device sees FLOAT (FIX -> FLOAT once converged under FIX)
    for (std::size_t i = 2; i < rec->modes().size(); ++i) {
        EXPECT_NE(rec->modes()[i] & MODEINITFLOAT_BIT, 0)
            << "iter " << i << " missing FLOAT bit";
        EXPECT_NE(rec->modes()[i] & MODEDCOP_BIT, 0)
            << "iter " << i << " lost MODEDCOP bit";
    }

    // The Circuit's integrator_ctx.mode must be restored to the original
    // value on return (so gmin/source stepping retries see the right bits).
    EXPECT_EQ(ckt.integrator_ctx.mode, saved_mode);
}

TEST(NewtonInitFlip, NoFlipWhenJCTBitAbsent) {
    // If the driver starts with MODEINITFIX (e.g. gmin stepping), the
    // ngspice cascade is: FIX (iter 0) -> FLOAT once converged (iter 1) ->
    // return converged from FLOAT (iter 2).
    Circuit ckt;
    int32_t na = ckt.node("a");
    auto *rec = new ModeRecorder("XMODE", na);
    ckt.add_device(std::unique_ptr<Device>(rec));
    ckt.finalize();

    ckt.integrator_ctx.mode = MODEDCOP_BIT | MODEINITFIX_BIT;
    const int saved_mode = ckt.integrator_ctx.mode;

    KLUSolver solver;
    solver.symbolic(ckt.pattern());
    std::vector<double> solution(ckt.num_vars(), 0.0);
    SimOptions opts;
    opts.max_iter = 10;
    auto result = newton_solve(ckt, solver, solution, opts);

    ASSERT_TRUE(result.converged);

    // iter 0: device sees FIX (the starting mode)
    ASSERT_GE(rec->modes().size(), 1u);
    EXPECT_EQ(rec->modes()[0], MODEDCOP_BIT | MODEINITFIX_BIT);

    // Restored on return
    EXPECT_EQ(ckt.integrator_ctx.mode, saved_mode);
}

TEST(NewtonInitFlip, MODEINITTRANFlipsToFLOAT) {
    // First transient step: MODEINITTRAN -> MODEINITFLOAT after iter 0.
    Circuit ckt;
    int32_t na = ckt.node("a");
    auto *rec = new ModeRecorder("XMODE", na);
    ckt.add_device(std::unique_ptr<Device>(rec));
    ckt.finalize();

    ckt.integrator_ctx.mode = MODETRAN_BIT | MODEINITTRAN_BIT;
    const int saved_mode = ckt.integrator_ctx.mode;

    KLUSolver solver;
    solver.symbolic(ckt.pattern());
    std::vector<double> solution(ckt.num_vars(), 0.0);
    SimOptions opts;
    opts.max_iter = 10;
    auto result = newton_solve(ckt, solver, solution, opts);

    ASSERT_TRUE(result.converged);
    ASSERT_GE(rec->modes().size(), 2u);

    // iter 0: device sees MODEINITTRAN
    EXPECT_NE(rec->modes()[0] & MODEINITTRAN_BIT, 0);

    // iter 1+: device sees MODEINITFLOAT (TRAN -> FLOAT after iter 0)
    for (std::size_t i = 1; i < rec->modes().size(); ++i) {
        EXPECT_NE(rec->modes()[i] & MODEINITFLOAT_BIT, 0)
            << "iter " << i << " missing FLOAT bit";
        EXPECT_EQ(rec->modes()[i] & MODEINITTRAN_BIT, 0)
            << "iter " << i << " still has TRAN bit";
        // MODETRAN base bit preserved
        EXPECT_NE(rec->modes()[i] & MODETRAN_BIT, 0)
            << "iter " << i << " lost MODETRAN bit";
    }

    EXPECT_EQ(ckt.integrator_ctx.mode, saved_mode);
}

TEST(NewtonInitFlip, MODEINITPREDFlipsToFLOAT) {
    // Subsequent transient steps: MODEINITPRED -> MODEINITFLOAT after iter 0.
    Circuit ckt;
    int32_t na = ckt.node("a");
    auto *rec = new ModeRecorder("XMODE", na);
    ckt.add_device(std::unique_ptr<Device>(rec));
    ckt.finalize();

    ckt.integrator_ctx.mode = MODETRAN_BIT | MODEINITPRED_BIT;
    const int saved_mode = ckt.integrator_ctx.mode;

    KLUSolver solver;
    solver.symbolic(ckt.pattern());
    std::vector<double> solution(ckt.num_vars(), 0.0);
    SimOptions opts;
    opts.max_iter = 10;
    auto result = newton_solve(ckt, solver, solution, opts);

    ASSERT_TRUE(result.converged);
    ASSERT_GE(rec->modes().size(), 2u);

    // iter 0: device sees MODEINITPRED
    EXPECT_NE(rec->modes()[0] & MODEINITPRED_BIT, 0);

    // iter 1+: device sees MODEINITFLOAT (PRED -> FLOAT after iter 0)
    for (std::size_t i = 1; i < rec->modes().size(); ++i) {
        EXPECT_NE(rec->modes()[i] & MODEINITFLOAT_BIT, 0)
            << "iter " << i << " missing FLOAT bit";
        EXPECT_EQ(rec->modes()[i] & MODEINITPRED_BIT, 0)
            << "iter " << i << " still has PRED bit";
        EXPECT_NE(rec->modes()[i] & MODETRAN_BIT, 0)
            << "iter " << i << " lost MODETRAN bit";
    }

    EXPECT_EQ(ckt.integrator_ctx.mode, saved_mode);
}
