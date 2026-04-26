// Verifies that SimOptions.temp is threaded through the Circuit's
// IntegratorCtx to state-storing devices during newton_solve.  The
// BSIM4v7Device adapter depends on this so .options temp=XXX overrides
// the hard-coded T_NOMINAL in bsim4v7_device.cpp.

#include <gtest/gtest.h>

#include "core/circuit.hpp"
#include "core/linear_solver.hpp"
#include "core/newton.hpp"
#include "core/types.hpp"
#include "devices/device.hpp"

#include <vector>

using namespace neospice;

namespace {

// Probe device: records tls_integrator_ctx->options->temp on every
// evaluate() call.  Also stamps a trivial diagonal so Newton converges.
class TempProbe : public Device {
public:
    TempProbe(std::string name, int32_t node)
        : Device(std::move(name)), node_(node) {}

    void stamp_pattern(SparsityBuilder& builder) const override {
        builder.add(node_, node_);
    }
    void assign_offsets(const SparsityPattern& pattern) override {
        off_ = pattern.offset(node_, node_);
    }
    void evaluate(const std::vector<double>&, NumericMatrix& mat,
                  std::vector<double>&) override {
        auto* ic = tls_integrator_ctx;
        temps_.push_back(
            (ic && ic->options) ? ic->options->temp : -1.0);
        mat.add(off_, 1.0);
    }
    const std::vector<double>& temps() const { return temps_; }

private:
    int32_t node_;
    MatrixOffset off_ = -1;
    std::vector<double> temps_;
};

} // namespace

TEST(SimOptionsPlumbing, NonDefaultTempReachesDeviceViaIntegratorCtx) {
    Circuit ckt;
    int32_t na = ckt.node("a");
    auto* probe = new TempProbe("XPROBE", na);
    ckt.add_device(std::unique_ptr<Device>(probe));
    ckt.finalize();

    // Caller sets a non-default temperature (77 K = liquid nitrogen).
    ckt.options.temp = 77.0;
    ckt.integrator_ctx.options = &ckt.options;

    auto solver = create_solver(ckt.pattern().size());
    solver->symbolic(ckt.pattern());
    std::vector<double> solution(ckt.num_vars(), 0.0);
    auto result = newton_solve(ckt, *solver, solution, ckt.options);
    ASSERT_TRUE(result.converged);

    ASSERT_FALSE(probe->temps().empty());
    for (double t : probe->temps()) {
        EXPECT_DOUBLE_EQ(t, 77.0);
    }
}

TEST(SimOptionsPlumbing, DefaultTempIsTNominal) {
    Circuit ckt;
    int32_t na = ckt.node("a");
    auto* probe = new TempProbe("XPROBE", na);
    ckt.add_device(std::unique_ptr<Device>(probe));
    ckt.finalize();

    ckt.integrator_ctx.options = &ckt.options;

    auto solver = create_solver(ckt.pattern().size());
    solver->symbolic(ckt.pattern());
    std::vector<double> solution(ckt.num_vars(), 0.0);
    auto result = newton_solve(ckt, *solver, solution, ckt.options);
    ASSERT_TRUE(result.converged);

    ASSERT_FALSE(probe->temps().empty());
    for (double t : probe->temps()) {
        EXPECT_DOUBLE_EQ(t, T_NOMINAL);
    }
}
