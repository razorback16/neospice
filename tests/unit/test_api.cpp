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
    ASSERT_TRUE(result.dc.has_value());
    EXPECT_NEAR(result.dc->node_voltages["v(out)"], 5.0, 1e-6);
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
    ASSERT_TRUE(result.transient.has_value());
    EXPECT_GT(result.transient->time.size(), 10u);
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
    ASSERT_TRUE(result.ac.has_value());
    EXPECT_GT(result.ac->frequency.size(), 10u);
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
    ASSERT_TRUE(result.transient.has_value());
    auto& tran = *result.transient;

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
    ASSERT_TRUE(result.dc.has_value());

    auto names = result.dc->signal_names();
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
    ASSERT_TRUE(result.dc_sweep.has_value());
    auto& sw = *result.dc_sweep;

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
    ASSERT_TRUE(result.ac.has_value());
    auto& ac = *result.ac;

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
    ASSERT_TRUE(result.noise.has_value());
    auto& nr = *result.noise;

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
    ASSERT_TRUE(result.dc.has_value());
    EXPECT_TRUE(result.dc->status.converged);
    EXPECT_GT(result.dc->status.iterations, 0);
    EXPECT_GT(result.dc->status.elapsed_seconds, 0.0);
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
    ASSERT_TRUE(result.transient.has_value());
    EXPECT_TRUE(result.transient->status.converged);
    EXPECT_GT(result.transient->status.iterations, 0);
    EXPECT_GT(result.transient->status.elapsed_seconds, 0.0);
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
    ASSERT_TRUE(result.ac.has_value());
    EXPECT_TRUE(result.ac->status.converged);
    EXPECT_GT(result.ac->status.elapsed_seconds, 0.0);
}
