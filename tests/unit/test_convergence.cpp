#include <gtest/gtest.h>
#include "core/dc.hpp"
#include "core/convergence.hpp"
#include "core/ckt_mode.hpp"
#include "core/circuit.hpp"
#include "core/neo_solver.hpp"
#include "api/neospice.hpp"
#include "parser/netlist_parser.hpp"
#include <array>

using namespace neospice;

TEST(Convergence, RejectsDeviceFailureWithSettledTerminalVoltages) {
    // ngspice 47 preserves the DEVconvTest failure flag on the NIconvTest
    // return path. Stable node voltages alone must not override
    // a device reporting that its internal state has not converged.
    class ConvergenceProbe : public Device {
    public:
        ConvergenceProbe(int32_t node, bool settled)
            : Device("convergence_probe"), node_(node), settled_(settled) {}
        void stamp_pattern(SparsityBuilder& builder) const override { builder.add(node_, node_); }
        void assign_offsets(const SparsityPattern& pattern) override { off_ = pattern.offset(node_, node_); }
        void evaluate(const std::vector<double>&, NumericMatrix& mat, std::span<double> rhs) override {
            mat.add(off_, 1.0);
            rhs[node_] += 1.0;
        }
        bool device_converged(const std::vector<double>&) const override {
            ++checks;
            return settled_;
        }
        mutable int checks = 0;
    private:
        int32_t node_;
        bool settled_;
        MatrixOffset off_ = -1;
    };
    for (bool settled : {false, true}) {
        SCOPED_TRACE(settled);
        Circuit circuit;
        const auto node = static_cast<int32_t>(circuit.node("probe"));
        auto device = std::make_unique<ConvergenceProbe>(node, settled);
        auto* probe = device.get();
        circuit.add_device(std::move(device));
        circuit.finalize();
        circuit.integrator_ctx.mode = MODEDCOP_BIT | MODEINITJCT_BIT;
        NeoSolver solver;
        solver.symbolic(circuit.pattern());
        std::vector<double> solution(circuit.num_vars(), 0.0);
        const auto result = newton_solve(circuit, solver, solution, circuit.options);
        EXPECT_EQ(result.converged, settled);
        EXPECT_GT(probe->checks, 0);
        EXPECT_NEAR(solution[node], 1.0, 1e-12);
    }
}

TEST(Convergence, DiodeDC) {
    std::string netlist = R"(
Diode Convergence Test
V1 top 0 DC 5.0
R1 top mid 1k
D1 mid 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);
    EXPECT_GT(result.voltage("mid"), 0.5);
    EXPECT_LT(result.voltage("mid"), 0.9);
    EXPECT_NEAR(result.voltage("top"), 5.0, 1e-6);
}

TEST(Convergence, GminSteppingWorks) {
    // Same diode circuit - should converge via normal Newton or gmin stepping
    std::string netlist = R"(
Gmin Stepping Test
V1 top 0 DC 0.7
R1 top mid 100
D1 mid 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);
    // Diode forward voltage should be around 0.5-0.7V
    EXPECT_GT(result.voltage("mid"), 0.3);
    EXPECT_LT(result.voltage("mid"), 0.75);
}

TEST(Convergence, SourceSteppingSimpleCircuit) {
    // A simple resistor-diode circuit that converges via normal Newton.
    // This is a regression test: true source stepping must not break
    // circuits that already converge easily.
    std::string netlist = R"(
Source Stepping Regression
V1 vdd 0 DC 3.3
R1 vdd out 1k
D1 out 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);
    // Diode forward voltage ~0.6V, VDD = 3.3V
    EXPECT_NEAR(result.voltage("vdd"), 3.3, 1e-6);
    EXPECT_GT(result.voltage("out"), 0.5);
    EXPECT_LT(result.voltage("out"), 0.8);
}

TEST(Convergence, SourceSteppingCrossCoupledInverters) {
    // Cross-coupled CMOS inverter pair (bistable latch) with a slight
    // asymmetry (different W on M4 vs M2) so the circuit is biased toward
    // one stable state.  Without source stepping, Newton may fail to find
    // the DC operating point; with it, the gradual VDD ramp guides the
    // circuit into a stable latch state.
    std::string netlist = R"(
Cross-coupled CMOS inverters
VDD vdd 0 DC 5.0
* Inverter 1: input = q, output = qb
M1 qb q vdd vdd PMOD W=10u L=1u
M2 qb q 0   0   NMOD W=5u  L=1u
* Inverter 2: input = qb, output = q (slightly weaker NMOS to break symmetry)
M3 q qb vdd vdd PMOD W=10u L=1u
M4 q qb 0   0   NMOD W=4u  L=1u
* Pull-down on q to bias toward state B (q low, qb high)
R1 q 0 100k
.model NMOD NMOS(LEVEL=1 VTO=0.7 KP=110u GAMMA=0.4 PHI=0.65)
.model PMOD PMOS(LEVEL=1 VTO=-0.7 KP=50u GAMMA=0.57 PHI=0.65)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);
    double vq  = result.voltage("q");
    double vqb = result.voltage("qb");
    // The latch must settle to one of the two stable states:
    //   state A: q ~ VDD (5V), qb ~ 0V
    //   state B: q ~ 0V,       qb ~ VDD (5V)
    // Due to the pull-down on q, state B is strongly favored.
    bool state_a = (vq > 4.0 && vqb < 1.0);
    bool state_b = (vq < 1.0 && vqb > 4.0);
    EXPECT_TRUE(state_a || state_b)
        << "Expected bistable latch to converge to a stable state, got v(q)="
        << vq << " v(qb)=" << vqb;
    EXPECT_NEAR(result.voltage("vdd"), 5.0, 1e-6);
}

TEST(Convergence, SourceSteppingWithCurrentSource) {
    // Verify that current sources are also scaled during source stepping.
    // A current source drives a diode — the DC operating point depends on
    // the source value.
    std::string netlist = R"(
Current Source Stepping
I1 0 out DC 1m
D1 out 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);
    // 1 mA through a diode => forward voltage ~0.6V
    EXPECT_GT(result.voltage("out"), 0.5);
    EXPECT_LT(result.voltage("out"), 0.75);
}

TEST(Convergence, PseudoTransientSimpleCircuit) {
    // Verify that pseudo-transient continuation does not break a simple
    // circuit that already converges via Newton.  This is a regression test:
    // the full convergence chain (Newton -> gmin -> source -> pseudo-transient)
    // must still produce correct results.
    std::string netlist = R"(
Pseudo-Transient Regression
V1 vdd 0 DC 3.3
R1 vdd out 1k
D1 out 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);
    EXPECT_NEAR(result.voltage("vdd"), 3.3, 1e-6);
    EXPECT_GT(result.voltage("out"), 0.5);
    EXPECT_LT(result.voltage("out"), 0.8);
}

TEST(Convergence, PseudoTransientDirectCall) {
    // Call pseudo_transient() directly on a circuit that converges easily
    // to verify the function itself works correctly and returns a converged
    // result with a valid solution.
    std::string netlist = R"(
Pseudo-Transient Direct
V1 top 0 DC 5.0
R1 top mid 1k
D1 mid 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);

    const int32_t n = ckt.num_vars();
    std::vector<double> solution(n, 0.0);

    auto solver = std::make_unique<NeoSolver>();
    solver->symbolic(ckt.pattern());
    ckt.integrator_ctx.options = &ckt.options;
    ckt.integrator_ctx.mode = 0x10 | 0x400;  // MODEDCOP | MODEINITFIX

    auto result = pseudo_transient(ckt, *solver, solution, ckt.options);
    EXPECT_TRUE(result.converged);
    // Verify the solution vector has the right size (solution is modified in-place)
    EXPECT_EQ(solution.size(), static_cast<size_t>(n));
}

TEST(Convergence, PseudoTransientCrossCoupledMOS) {
    // Cross-coupled CMOS inverters -- a known-difficult circuit for DC
    // convergence.  The full convergence chain should handle this via
    // one of the convergence aids.
    std::string netlist = R"(
Pseudo-Transient Cross-Coupled
VDD vdd 0 DC 5.0
M1 qb q vdd vdd PMOD W=10u L=1u
M2 qb q 0   0   NMOD W=5u  L=1u
M3 q qb vdd vdd PMOD W=10u L=1u
M4 q qb 0   0   NMOD W=4u  L=1u
R1 q 0 100k
.model NMOD NMOS(LEVEL=1 VTO=0.7 KP=110u GAMMA=0.4 PHI=0.65)
.model PMOD PMOS(LEVEL=1 VTO=-0.7 KP=50u GAMMA=0.57 PHI=0.65)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);
    double vq  = result.voltage("q");
    double vqb = result.voltage("qb");
    // Must converge to one of the two stable states
    bool state_a = (vq > 4.0 && vqb < 1.0);
    bool state_b = (vq < 1.0 && vqb > 4.0);
    EXPECT_TRUE(state_a || state_b)
        << "Expected latch to converge, got v(q)=" << vq << " v(qb)=" << vqb;
}

TEST(Convergence, OpTransientDirectCall) {
    // transient_operating_point() (OPtran) is the final fallback in the DC
    // convergence chain (mirrors ngspice CKTop -> OPtran).  Verify it runs a
    // minimal transient to a relaxed operating point on a circuit it can solve.
    std::string netlist = R"(
OPtran Direct
V1 top 0 DC 5.0
R1 top mid 1k
D1 mid 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);

    const int32_t n = ckt.num_vars();
    std::vector<double> solution(n, 0.0);

    auto solver = std::make_unique<NeoSolver>();
    solver->symbolic(ckt.pattern());
    ckt.integrator_ctx.options = &ckt.options;
    ckt.integrator_ctx.mode = 0x10 | 0x200;  // MODEDCOP | MODEINITJCT

    auto result = transient_operating_point(ckt, *solver, solution, ckt.options);
    EXPECT_TRUE(result.converged);
    EXPECT_EQ(solution.size(), static_cast<size_t>(n));
}

TEST(Convergence, OpTransientRescuesIntegratorCell) {
    // The LTspice "Integral" idt cell: a VCCS charging a 1F capacitor whose
    // node has no resistive DC path (only the cap + an .ic).  Direct Newton
    // and the gmin/source aids cannot settle it, but the OPtran fallback
    // relaxes the capacitor from its initial condition to the DC equilibrium.
    // ngspice resolves this exact circuit only via its OPtran (Transient op).
    std::string netlist = R"(
OPtran Integrator Cell
G1 0 n1 u 0 1
C1 n1 0 1
R1 n1 n2 1
E1 n2 0 n1 0 1
Eout y 0 n1 0 1
Vu u 0 DC 0
RLoad y 0 10k
.ic V(n1)=0
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);
    // With zero input the integrator output relaxes to 0 V.
    EXPECT_NEAR(result.voltage("y"), 0.0, 1e-6);
    EXPECT_EQ(result.status.convergence_method, ConvergenceMethod::OP_TRANSIENT);
}

TEST(Convergence, OpTransientDoesNotPerturbSimpleCircuit) {
    // Regression: OPtran is the last fallback and must never run (nor alter the
    // result) for a circuit that already converges via direct Newton.
    std::string netlist = R"(
OPtran No-Perturb
V1 vdd 0 DC 3.3
R1 vdd out 1k
D1 out 0 DMOD
.model DMOD D(IS=1e-14 N=1)
.op
.end
)";
    Simulator sim;
    auto ckt = sim.parse(netlist);
    DCResult result = sim.run_dc(ckt);
    EXPECT_NEAR(result.voltage("vdd"), 3.3, 1e-6);
    EXPECT_GT(result.voltage("out"), 0.5);
    EXPECT_LT(result.voltage("out"), 0.8);
    EXPECT_EQ(result.status.convergence_method, ConvergenceMethod::DIRECT);
}

TEST(Convergence, OpTransientSeedsReferenceStartupHistory) {
    // optran.c initializes every CKTdeltaOld slot to CKTmaxStep before
    // shifting in each attempted interval. The initial history is not the
    // reduced startup step: it remains visible to second-order truncation.
    class HistoryProbe : public Device {
    public:
        explicit HistoryProbe(int32_t node) : Device("history_probe"), node_(node) {}
        int32_t state_vars() const override { return 1; }
        void set_state_ptrs(double*, double* state1, double* state2, double*, int32_t base) override {
            state1_ = state1 + base;
            state2_ = state2 + base;
        }
        void stamp_pattern(SparsityBuilder& builder) const override { builder.add(node_, node_); }
        void assign_offsets(const SparsityPattern& pattern) override { off_ = pattern.offset(node_, node_); }
        void evaluate(const std::vector<double>&, NumericMatrix& mat, std::span<double> rhs) override {
            const auto& ctx = *tls_integrator_ctx;
            if (times.empty()) initial_states = {*state1_, *state2_};
            if (times.empty() || times.back() != ctx.current_time) {
                times.push_back(ctx.current_time);
                intervals.push_back({ctx.delta_old[0], ctx.delta_old[1], ctx.delta_old[2]});
            }
            mat.add(off_, 1.0);
            rhs[node_] += 1.0;
        }
        double compute_trunc(const IntegratorCtx& ctx, const SimOptions&) const override {
            return 2.0 * ctx.delta;
        }
        std::vector<double> times;
        std::vector<std::array<double, 3>> intervals;
        std::array<double, 2> initial_states{};
    private:
        int32_t node_;
        MatrixOffset off_ = -1;
        double* state1_ = nullptr;
        double* state2_ = nullptr;
    };
    Circuit circuit;
    const auto node = static_cast<int32_t>(circuit.node("probe"));
    auto device = std::make_unique<HistoryProbe>(node);
    auto* probe = device.get();
    circuit.add_device(std::move(device));
    circuit.finalize();
    circuit.state0()[0] = 3.5;
    circuit.state1()[0] = -2.0;
    circuit.state2()[0] = 19.0;
    NeoSolver solver;
    solver.symbolic(circuit.pattern());
    std::vector<double> solution(circuit.num_vars(), 0.0);
    const auto result = transient_operating_point(circuit, solver, solution, circuit.options);
    ASSERT_TRUE(result.converged);
    ASSERT_GE(probe->intervals.size(), 3u);
    // Initial MODEINITTRAN predictors need the entry state in both older
    // slots. ngspice copies state0 to state1 and rotates before the first load.
    EXPECT_DOUBLE_EQ(probe->initial_states[0], 3.5);
    EXPECT_DOUBLE_EQ(probe->initial_states[1], 3.5);
    const std::array<std::array<double, 3>, 3> expected{{
        {1e-9, 100e-9, 100e-9}, {1e-9, 1e-9, 100e-9}, {2e-9, 1e-9, 1e-9}}};
    for (size_t step = 0; step < expected.size(); ++step)
        for (size_t history = 0; history < 3; ++history)
            EXPECT_NEAR(probe->intervals[step][history], expected[step][history], 1e-22)
                << "step=" << step << " history=" << history;
    EXPECT_NEAR(solution[node], 1.0, 1e-12);
}

TEST(Convergence, OpTransientUsesSecondOrderStepProposal) {
    // A controlled device LTE constraint distinguishes keeping first order
    // (0.95*h) from promoting to second order (1.5*h). In both cases ngspice's
    // OPtran uses the second-order probe's proposed interval for the next step.
    class StepProbe : public Device {
    public:
        StepProbe(int32_t node, double ratio) : Device("step_probe"), node_(node), ratio_(ratio) {}
        void stamp_pattern(SparsityBuilder& builder) const override { builder.add(node_, node_); }
        void assign_offsets(const SparsityPattern& pattern) override { off_ = pattern.offset(node_, node_); }
        void evaluate(const std::vector<double>&, NumericMatrix& mat, std::span<double> rhs) override {
            const auto& ctx = *tls_integrator_ctx;
            if (times.empty() || times.back() != ctx.current_time) {
                times.push_back(ctx.current_time);
                orders.push_back(ctx.order);
            }
            mat.add(off_, 1.0);
            rhs[node_] += 1.0;
        }
        double compute_trunc(const IntegratorCtx& ctx, const SimOptions&) const override {
            if (ctx.current_time > 1.5e-9 && ctx.current_time < 2.5e-9 && ctx.order == 2)
                return ratio_ * ctx.delta;
            return 2.0 * ctx.delta;
        }
        std::vector<double> times;
        std::vector<int> orders;
    private:
        int32_t node_;
        double ratio_;
        MatrixOffset off_ = -1;
    };

    for (double ratio : {0.95, 1.5}) {
        SCOPED_TRACE(ratio);
        Circuit circuit;
        const int32_t node = static_cast<int32_t>(circuit.node("probe"));
        auto device = std::make_unique<StepProbe>(node, ratio);
        auto* probe = device.get();
        circuit.add_device(std::move(device));
        circuit.finalize();
        NeoSolver solver;
        solver.symbolic(circuit.pattern());
        std::vector<double> solution(circuit.num_vars(), 0.0);
        const auto result = transient_operating_point(circuit, solver, solution, circuit.options);
        ASSERT_TRUE(result.converged);
        ASSERT_GE(probe->times.size(), 3u);
        EXPECT_NEAR(probe->times[0], 1e-9, 1e-22);
        EXPECT_NEAR(probe->times[1], 2e-9, 1e-22);
        EXPECT_NEAR(probe->times[2] - probe->times[1], ratio * 1e-9, 1e-22);
        EXPECT_EQ(probe->orders[2], ratio < 1.05 ? 1 : 2);
        EXPECT_NEAR(solution[node], 1.0, 1e-12);
    }
}

TEST(Convergence, TrueGminUsesNgspice47SlowStepFactorFloor) {
    // Driver-contract probe: the first attempted conductance fails. The next
    // succeeds just below/above 3*itl2/4 iterations, so the adaptive factor
    // falls below three unless the ngspice47 new_gmin floor is preserved.
    class SlowStep : public Device {
    public:
        SlowStep(int32_t node, int settle_loads)
            : Device("slow_gmin_probe"), node_(node), settle_loads_(settle_loads) {}
        void stamp_pattern(SparsityBuilder& builder) const override { builder.add(node_, node_); }
        void assign_offsets(const SparsityPattern& pattern) override { offset_ = pattern.offset(node_, node_); }
        void evaluate(const std::vector<double>&, NumericMatrix& matrix, std::span<double> rhs) override {
            const double g = tls_integrator_ctx->options->gmin;
            if (levels.empty() || levels.back() != g) {
                levels.push_back(g);
                loads_ = 0;
            }
            ++loads_;
            matrix.add(offset_, 1);
            rhs[node_] += 1;
        }
        bool device_converged(const std::vector<double>&) const override {
            return levels.size() > 1 && (levels.size() != 2 || loads_ >= settle_loads_);
        }
        std::vector<double> levels;
    private:
        int32_t node_;
        int settle_loads_;
        int loads_ = 0;
        MatrixOffset offset_ = -1;
    };
    for (int settle_loads : {37, 38}) {
        SCOPED_TRACE(settle_loads);
        Circuit circuit;
        const auto node = static_cast<int32_t>(circuit.node("probe"));
        auto device = std::make_unique<SlowStep>(node, settle_loads);
        const auto* probe = device.get();
        circuit.add_device(std::move(device));
        circuit.finalize();
        circuit.integrator_ctx.options = &circuit.options;
        NeoSolver solver;
        solver.symbolic(circuit.pattern());
        std::vector<double> solution(circuit.num_vars(), 0);
        const auto result = true_gmin_stepping(circuit, solver, solution, circuit.options,
            MODEDCOP_BIT | MODEINITFLOAT_BIT, MODEDCOP_BIT | MODEINITFLOAT_BIT);
        ASSERT_TRUE(result.converged);
        ASSERT_GE(probe->levels.size(), 3u);
        EXPECT_DOUBLE_EQ(probe->levels[0], 0.001);
        const double retry_factor = std::sqrt(std::sqrt(10.0));
        EXPECT_DOUBLE_EQ(probe->levels[1], 0.01 / retry_factor);
        const double next_factor = settle_loads > 37 ? 3.0 : retry_factor;
        EXPECT_DOUBLE_EQ(probe->levels[2], probe->levels[1] / next_factor);
        EXPECT_DOUBLE_EQ(circuit.options.gmin, 1e-12);
        EXPECT_DOUBLE_EQ(solution[node], 1.0);
    }
}
