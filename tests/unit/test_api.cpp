#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include "api/neospice.hpp"

using namespace neospice;

TEST(SimulatorAPI, ParseAndRunDC) {
    Simulator sim;
    std::string netlist = R"(
Voltage Divider
V1 in 0 10
R1 in out 1k
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_NEAR(std::get<DCResult>(result.analysis).node_voltages["v(out)"], 5.0, 1e-6);
}

TEST(SimulatorAPI, RunTransient) {
    Simulator sim;
    std::string netlist = R"(
RC
V1 in 0 5
R1 in out 1k
C1 out 0 1u
.tran 10u 5m
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(result.analysis));
    EXPECT_GT(std::get<TransientResult>(result.analysis).time.size(), 10u);
}

TEST(SimulatorAPI, RunAC) {
    Simulator sim;
    std::string netlist = R"(
RC Lowpass
V1 in 0 DC 0 AC 1
R1 in out 1k
C1 out 0 1n
.ac dec 10 100 10meg
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<ACResult>(result.analysis));
    EXPECT_GT(std::get<ACResult>(result.analysis).frequency.size(), 10u);
}

TEST(TransientResultAPI, DiffVoltage) {
    neospice::Simulator sim;
    std::string netlist = R"(
Diff test
V1 a 0 5
V2 b 0 3
R1 a 0 1k
R2 b 0 1k
.tran 1u 10u
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(result.analysis));
    auto& tran = std::get<TransientResult>(result.analysis);

    // diff() returns v(a) - v(b) at every time point
    auto d = tran.diff("a", "b");
    ASSERT_EQ(d.size(), tran.time.size());
    for (auto v : d)
        EXPECT_NEAR(v, 2.0, 1e-6);

    // signal_names() lists all available signals
    auto names = tran.signal_names();
    EXPECT_TRUE(std::find(names.begin(), names.end(), "v(a)") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "v(b)") != names.end());
}

TEST(DCResultAPI, SignalNames) {
    neospice::Simulator sim;
    std::string netlist = R"(
DC signals
V1 in 0 10
R1 in out 1k
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));

    auto names = std::get<DCResult>(result.analysis).signal_names();
    EXPECT_TRUE(std::find(names.begin(), names.end(), "v(out)") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "v(in)") != names.end());
}

TEST(DCSweepResultAPI, DiffAndSignalNames) {
    neospice::Simulator sim;
    std::string netlist = R"(
Sweep diff
V1 in 0 5
R1 in a 1k
R2 in b 2k
R3 a 0 1k
R4 b 0 1k
.dc V1 0 10 1
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCSweepResult>(result.analysis));
    auto& sw = std::get<DCSweepResult>(result.analysis);

    auto d = sw.diff("a", "b");
    ASSERT_EQ(d.size(), sw.sweep_values.size());

    auto names = sw.signal_names();
    EXPECT_TRUE(std::find(names.begin(), names.end(), "v(a)") != names.end());
}

TEST(ACResultAPI, CurrentMagnitudeAndSignalNames) {
    neospice::Simulator sim;
    std::string netlist = R"(
AC current
V1 in 0 DC 0 AC 1
R1 in 0 1k
.ac dec 10 100 1meg
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<ACResult>(result.analysis));
    auto& ac = std::get<ACResult>(result.analysis);

    // Current magnitude in dB
    auto idb = ac.current_magnitude_db("v1");
    ASSERT_EQ(idb.size(), ac.frequency.size());
    // At all frequencies, I(V1) = V/R = 1/1000 => 20*log10(1e-3) = -60 dB
    for (auto v : idb)
        EXPECT_NEAR(v, -60.0, 0.1);

    // Current phase
    auto iph = ac.current_phase_deg("v1");
    ASSERT_EQ(iph.size(), ac.frequency.size());

    // signal_names
    auto names = ac.signal_names();
    EXPECT_FALSE(names.empty());
}

TEST(NoiseResultAPI, Helpers) {
    neospice::Simulator sim;
    std::string netlist = R"(
Noise test
V1 in 0 DC 0 AC 1
R1 in out 1k
R2 out 0 1k
.noise v(out) V1 dec 10 1 1meg
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<NoiseResult>(result.analysis));
    auto& nr = std::get<NoiseResult>(result.analysis);

    // output_noise_sqrt returns V/sqrt(Hz) (more intuitive than V^2/Hz)
    auto on_sqrt = nr.output_noise_sqrt();
    ASSERT_EQ(on_sqrt.size(), nr.frequency.size());
    for (std::size_t i = 0; i < on_sqrt.size(); ++i)
        EXPECT_NEAR(on_sqrt[i], std::sqrt(nr.output_noise_density[i]), 1e-30);

    // input_noise_sqrt
    auto in_sqrt = nr.input_noise_sqrt();
    ASSERT_EQ(in_sqrt.size(), nr.frequency.size());

    // integrated_output_noise over full band (trapezoidal integration)
    double integrated = nr.integrated_output_noise(nr.frequency.front(), nr.frequency.back());
    EXPECT_GT(integrated, 0.0);

    // device_names lists contributing devices
    auto devs = nr.device_names();
    EXPECT_FALSE(devs.empty());

    // signal_names
    auto snames = nr.signal_names();
    EXPECT_TRUE(std::find(snames.begin(), snames.end(), "onoise") != snames.end());
    EXPECT_TRUE(std::find(snames.begin(), snames.end(), "inoise") != snames.end());
}

TEST(NoiseResultAPI, DeviceNoiseDensity) {
    neospice::Simulator sim;
    std::string netlist = R"(
Noise per-device
V1 in 0 DC 0 AC 1
R1 in out 1k
R2 out 0 1k
.noise v(out) V1 dec 10 1 1meg
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<NoiseResult>(result.analysis));
    auto& nr = std::get<NoiseResult>(result.analysis);

    auto devs = nr.device_names();
    ASSERT_FALSE(devs.empty());
    for (const auto& d : devs) {
        const auto& density = nr.device_noise_density(d);
        ASSERT_EQ(density.size(), nr.frequency.size());
        for (auto v : density)
            EXPECT_GE(v, 0.0);
    }

    EXPECT_THROW(nr.device_noise_density("nonexistent"), std::out_of_range);
}

TEST(SimStatusIntegration, DCResultHasStatus) {
    neospice::Simulator sim;
    std::string netlist = R"(
Status test
V1 in 0 10
R1 in out 1k
R2 out 0 1k
.op
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCResult>(result.analysis));
    EXPECT_TRUE(std::get<DCResult>(result.analysis).status.converged);
    EXPECT_GT(std::get<DCResult>(result.analysis).status.iterations, 0);
    EXPECT_GT(std::get<DCResult>(result.analysis).status.elapsed_seconds, 0.0);
}

TEST(SimStatusIntegration, TransientResultHasStatus) {
    neospice::Simulator sim;
    std::string netlist = R"(
RC status
V1 in 0 5
R1 in out 1k
C1 out 0 1u
.tran 10u 5m
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(result.analysis));
    EXPECT_TRUE(std::get<TransientResult>(result.analysis).status.converged);
    EXPECT_GT(std::get<TransientResult>(result.analysis).status.iterations, 0);
    EXPECT_GT(std::get<TransientResult>(result.analysis).status.elapsed_seconds, 0.0);
}

TEST(SimStatusIntegration, ACResultHasStatus) {
    neospice::Simulator sim;
    std::string netlist = R"(
AC status
V1 in 0 DC 0 AC 1
R1 in out 1k
C1 out 0 1n
.ac dec 10 100 10meg
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<ACResult>(result.analysis));
    EXPECT_TRUE(std::get<ACResult>(result.analysis).status.converged);
    EXPECT_GT(std::get<ACResult>(result.analysis).status.elapsed_seconds, 0.0);
}

TEST(SimStatusIntegration, NoiseResultHasStatus) {
    neospice::Simulator sim;
    std::string netlist = R"(
Noise status
V1 in 0 DC 0 AC 1
R1 in out 1k
R2 out 0 1k
.noise v(out) V1 dec 10 1 1meg
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<NoiseResult>(result.analysis));
    EXPECT_TRUE(std::get<NoiseResult>(result.analysis).status.converged);
    EXPECT_GT(std::get<NoiseResult>(result.analysis).status.elapsed_seconds, 0.0);
}

TEST(SimStatusIntegration, TFResultHasStatus) {
    neospice::Simulator sim;
    std::string netlist = R"(
TF status
V1 in 0 10
R1 in out 1k
R2 out 0 1k
.tf v(out) V1
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TFResult>(result.analysis));
    EXPECT_TRUE(std::get<TFResult>(result.analysis).status.converged);
    EXPECT_GT(std::get<TFResult>(result.analysis).status.elapsed_seconds, 0.0);
}

TEST(SimStatusIntegration, SensResultHasStatus) {
    neospice::Simulator sim;
    std::string netlist = R"(
Sens status
V1 in 0 10
R1 in out 1k
R2 out 0 1k
.sens v(out)
.end
)";
    auto ckt = sim.parse(netlist);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<SensResult>(result.analysis));
    EXPECT_TRUE(std::get<SensResult>(result.analysis).status.converged);
    EXPECT_GT(std::get<SensResult>(result.analysis).status.elapsed_seconds, 0.0);
}

TEST(AnalysisChaining, TransientFromDC) {
    neospice::Simulator sim;
    auto ckt = sim.parse(R"(
Chain test
V1 in 0 10
R1 in out 1k
C1 out 0 1u
.op
.end
)");
    auto dc = sim.run_dc(ckt);
    EXPECT_NEAR(dc.voltage("out"), 10.0, 1e-6);

    auto ckt2 = sim.parse(R"(
Chain transient
V1 in 0 5
R1 in out 1k
C1 out 0 1u
.end
)");
    neospice::TransientOptions opts;
    opts.ic_from = &dc;
    auto tran = sim.run_transient(ckt2, 10e-6, 5e-3, opts);

    EXPECT_GT(tran.voltage("out").front(), 5.0);
}

TEST(AnalysisChaining, ACFromDC) {
    neospice::Simulator sim;
    auto ckt = sim.parse(R"(
AC chain
V1 in 0 DC 5 AC 1
R1 in out 1k
R2 out 0 1k
.op
.end
)");
    auto dc = sim.run_dc(ckt);

    auto ckt2 = sim.parse(R"(
AC from DC
V1 in 0 DC 5 AC 1
R1 in out 1k
R2 out 0 1k
.end
)");
    neospice::ACOptions opts;
    opts.op_from = &dc;
    auto ac = sim.run_ac(ckt2, neospice::ACMode::DEC, 10, 100, 1e6, opts);
    EXPECT_GT(ac.frequency.size(), 10u);
    auto gain = ac.magnitude_db("out");
    EXPECT_NEAR(gain[0], -6.02, 0.5);
}
