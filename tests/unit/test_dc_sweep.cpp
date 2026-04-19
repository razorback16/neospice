#include <gtest/gtest.h>
#include "core/dc.hpp"
#include "api/neospice.hpp"
#include "parser/netlist_parser.hpp"
#include "devices/vsource.hpp"
#include "devices/resistor.hpp"
#include "output/raw_writer.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>

using namespace neospice;
using std::make_unique;

// ─────────────────────────────────────────────────────────────────────────────
// 1.  Resistor-divider sweep via programmatic API
// ─────────────────────────────────────────────────────────────────────────────
TEST(DCSweep, ResistorDivider) {
    Circuit ckt;
    auto n_in  = ckt.node("in");
    auto n_out = ckt.node("out");
    ckt.add_device(make_unique<VSource>("V1", n_in, GROUND_INTERNAL, 0.0));
    ckt.add_device(make_unique<Resistor>("R1", n_in, n_out, 1000.0));
    ckt.add_device(make_unique<Resistor>("R2", n_out, GROUND_INTERNAL, 1000.0));
    ckt.finalize();

    std::vector<DCSweepParam> params;
    DCSweepParam p;
    p.source_name = "V1";
    p.start = 0.0;
    p.stop  = 5.0;
    p.step  = 1.0;
    params.push_back(p);

    DCSweepResult result = solve_dc_sweep(ckt, params);

    ASSERT_EQ(result.sweep_var, "v1");
    ASSERT_EQ(result.sweep_values.size(), 6u);  // 0,1,2,3,4,5

    // At each step V(out) = V1/2
    for (size_t i = 0; i < result.sweep_values.size(); ++i) {
        double v1  = result.sweep_values[i];
        double out = result.voltages.at("v(out)")[i];
        EXPECT_NEAR(out, v1 / 2.0, 1e-6)
            << "Failed at sweep point V1=" << v1;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 2.  Resistor-divider sweep via Simulator::run_dc_sweep
// ─────────────────────────────────────────────────────────────────────────────
TEST(DCSweep, ResistorDividerViaSimulator) {
    Circuit ckt;
    auto n_in  = ckt.node("in");
    auto n_out = ckt.node("out");
    ckt.add_device(make_unique<VSource>("V1", n_in, GROUND_INTERNAL, 0.0));
    ckt.add_device(make_unique<Resistor>("R1", n_in, n_out, 1000.0));
    ckt.add_device(make_unique<Resistor>("R2", n_out, GROUND_INTERNAL, 1000.0));
    ckt.finalize();

    Simulator sim;
    std::vector<DCSweepParam> params;
    DCSweepParam p;
    p.source_name = "V1";
    p.start = 0.0;
    p.stop  = 4.0;
    p.step  = 2.0;
    params.push_back(p);

    DCSweepResult result = sim.run_dc_sweep(ckt, params);

    ASSERT_EQ(result.sweep_values.size(), 3u);  // 0, 2, 4
    EXPECT_NEAR(result.voltages.at("v(out)")[0], 0.0, 1e-6);
    EXPECT_NEAR(result.voltages.at("v(out)")[1], 1.0, 1e-6);
    EXPECT_NEAR(result.voltages.at("v(out)")[2], 2.0, 1e-6);
}

// ─────────────────────────────────────────────────────────────────────────────
// 3.  Diode IV sweep (UCB DIODevice via netlist parser)
// ─────────────────────────────────────────────────────────────────────────────
TEST(DCSweep, DiodeIV) {
    const char* netlist = R"(
Diode IV Sweep
V1 anode 0 DC 0
D1 anode cathode DMOD
R1 cathode 0 1
.model DMOD D(IS=1e-14 N=1)
.dc V1 -1 0.6 0.1
.end
)";
    Simulator sim;
    Circuit ckt = sim.parse(netlist);
    SimulationResult result = sim.run(ckt);

    ASSERT_TRUE(result.dc_sweep.has_value());
    auto& sw = *result.dc_sweep;

    // Reverse bias: nearly zero current (V_cathode ≈ 0)
    // Forward bias around 0.6 V: exponential current
    ASSERT_FALSE(sw.sweep_values.empty());

    // Find the point closest to V1 = -1.0 (first point)
    EXPECT_NEAR(sw.voltages.at("v(anode)")[0], -1.0, 1e-3);
    // V(cathode) at reverse bias ≈ 0 (tiny leakage)
    EXPECT_NEAR(sw.voltages.at("v(cathode)")[0], 0.0, 1e-6);

    // At forward bias (last point, V1 ≈ 0.6 V), current should be > 10 uA
    // (with Is=1e-14, N=1, VT≈26mV: I = 1e-14*exp(0.6/0.026) ≈ 0.12 mA)
    size_t last = sw.sweep_values.size() - 1;
    double i_fwd = std::abs(sw.currents.at("i(v1)")[last]);
    EXPECT_GT(i_fwd, 1e-5);
}

// ─────────────────────────────────────────────────────────────────────────────
// 4.  Nested sweep
// ─────────────────────────────────────────────────────────────────────────────
TEST(DCSweep, NestedSweep) {
    // R1 from in1→out, R2 from in2→out, R3 from out→gnd (all 1k)
    // V(out) = (V1 + V2) / 3  (superposition with equal resistors)
    Circuit ckt;
    auto n_in1 = ckt.node("in1");
    auto n_in2 = ckt.node("in2");
    auto n_out = ckt.node("out");
    ckt.add_device(make_unique<VSource>("V1", n_in1, GROUND_INTERNAL, 0.0));
    ckt.add_device(make_unique<VSource>("V2", n_in2, GROUND_INTERNAL, 0.0));
    ckt.add_device(make_unique<Resistor>("R1", n_in1, n_out, 1000.0));
    ckt.add_device(make_unique<Resistor>("R2", n_in2, n_out, 1000.0));
    ckt.add_device(make_unique<Resistor>("R3", n_out, GROUND_INTERNAL, 1000.0));
    ckt.finalize();

    // .dc V1 0 2 1  V2 0 1 0.5
    // outer: V1 = {0, 1, 2}  inner: V2 = {0, 0.5, 1.0}
    // total points = 3 * 3 = 9
    std::vector<DCSweepParam> params;
    DCSweepParam p0;
    p0.source_name = "V1";  p0.start = 0; p0.stop = 2; p0.step = 1;
    DCSweepParam p1;
    p1.source_name = "V2";  p1.start = 0; p1.stop = 1; p1.step = 0.5;
    params.push_back(p0);
    params.push_back(p1);

    DCSweepResult result = solve_dc_sweep(ckt, params);

    // inner sweep var = V2
    EXPECT_EQ(result.sweep_var, "v2");
    ASSERT_EQ(result.sweep_values.size(), 9u);

    // Check V(out) for each point
    // Points are ordered: (V1=0,V2=0),(V1=0,V2=0.5),(V1=0,V2=1.0),
    //                     (V1=1,V2=0),(V1=1,V2=0.5),(V1=1,V2=1.0),
    //                     (V1=2,V2=0),(V1=2,V2=0.5),(V1=2,V2=1.0)
    double outer_vals[] = {0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 2.0, 2.0, 2.0};
    double inner_vals[] = {0.0, 0.5, 1.0, 0.0, 0.5, 1.0, 0.0, 0.5, 1.0};
    for (size_t i = 0; i < 9; ++i) {
        double expected_vout = (outer_vals[i] + inner_vals[i]) / 3.0;
        EXPECT_NEAR(result.voltages.at("v(out)")[i], expected_vout, 1e-6)
            << "Failed at point " << i
            << " (V1=" << outer_vals[i] << ", V2=" << inner_vals[i] << ")";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 5.  Parser: parse .dc netlist and run via Simulator::run()
// ─────────────────────────────────────────────────────────────────────────────
TEST(DCSweep, ParserAndRun) {
    const char* netlist = R"(
* Resistor divider sweep
V1 in 0 DC 0
R1 in out 1k
R2 out 0 1k
.dc V1 0 5 1
.end
)";
    Simulator sim;
    Circuit ckt = sim.parse(netlist);
    SimulationResult result = sim.run(ckt);

    ASSERT_TRUE(result.dc_sweep.has_value());
    auto& sw = *result.dc_sweep;
    ASSERT_EQ(sw.sweep_values.size(), 6u);

    for (size_t i = 0; i < sw.sweep_values.size(); ++i) {
        double v1  = sw.sweep_values[i];
        double out = sw.voltages.at("v(out)")[i];
        EXPECT_NEAR(out, v1 / 2.0, 1e-6);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 6.  Parser: nested sweep .dc V1 0 2 1  V2 0 1 0.5
// ─────────────────────────────────────────────────────────────────────────────
TEST(DCSweep, ParserNested) {
    const char* netlist = R"(
* Nested sweep
V1 in1 0 DC 0
V2 in2 0 DC 0
R1 in1 out 1k
R2 in2 out 1k
R3 out 0 1k
.dc V1 0 2 1 V2 0 1 0.5
.end
)";
    Simulator sim;
    Circuit ckt = sim.parse(netlist);
    SimulationResult result = sim.run(ckt);

    ASSERT_TRUE(result.dc_sweep.has_value());
    auto& sw = *result.dc_sweep;
    EXPECT_EQ(sw.sweep_var, "v2");
    ASSERT_EQ(sw.sweep_values.size(), 9u);

    // Spot-check last point: V1=2, V2=1 → V(out) = 3/3 = 1.0
    EXPECT_NEAR(sw.voltages.at("v(out)")[8], 1.0, 1e-6);
}

// ─────────────────────────────────────────────────────────────────────────────
// 7.  RAW writer: write and verify file is non-empty and well-formed
// ─────────────────────────────────────────────────────────────────────────────
TEST(DCSweep, RawWriterBasic) {
    Circuit ckt;
    auto n_in  = ckt.node("in");
    auto n_out = ckt.node("out");
    ckt.add_device(make_unique<VSource>("V1", n_in, GROUND_INTERNAL, 0.0));
    ckt.add_device(make_unique<Resistor>("R1", n_in, n_out, 1000.0));
    ckt.add_device(make_unique<Resistor>("R2", n_out, GROUND_INTERNAL, 1000.0));
    ckt.finalize();

    std::vector<DCSweepParam> params;
    DCSweepParam p;
    p.source_name = "V1";
    p.start = 0; p.stop = 2; p.step = 1;
    params.push_back(p);

    DCSweepResult result = solve_dc_sweep(ckt, params);

    // Write to a temp file
    std::string tmpfile = "/tmp/test_dc_sweep.raw";
    write_raw(tmpfile, result);

    // File should exist and have content
    ASSERT_TRUE(std::filesystem::exists(tmpfile));
    std::ifstream ifs(tmpfile, std::ios::binary);
    ASSERT_TRUE(ifs.is_open());

    // Read header line
    std::string line;
    std::getline(ifs, line);
    EXPECT_EQ(line, "Title: neospice DC Sweep Analysis");

    // Check "No. Points" line
    std::string full_header;
    std::getline(ifs, full_header); // Date
    std::getline(ifs, full_header); // Plotname
    std::getline(ifs, full_header); // Flags
    std::getline(ifs, full_header); // No. Variables
    std::getline(ifs, full_header); // No. Points
    EXPECT_EQ(full_header, "No. Points: 3");

    std::filesystem::remove(tmpfile);
}

// ─────────────────────────────────────────────────────────────────────────────
// 8.  Branch current reported during sweep
// ─────────────────────────────────────────────────────────────────────────────
TEST(DCSweep, BranchCurrentReported) {
    Circuit ckt;
    auto n_top = ckt.node("top");
    auto n_mid = ckt.node("mid");
    ckt.add_device(make_unique<VSource>("V1", n_top, GROUND_INTERNAL, 0.0));
    ckt.add_device(make_unique<Resistor>("R1", n_top, n_mid, 1000.0));
    ckt.add_device(make_unique<Resistor>("R2", n_mid, GROUND_INTERNAL, 1000.0));
    ckt.finalize();

    std::vector<DCSweepParam> params;
    DCSweepParam p;
    p.source_name = "V1";
    p.start = 0; p.stop = 2; p.step = 1;
    params.push_back(p);

    DCSweepResult result = solve_dc_sweep(ckt, params);

    // i(v1) should be present
    ASSERT_TRUE(result.currents.count("i(v1)") > 0);
    auto& iv = result.currents.at("i(v1)");
    ASSERT_EQ(iv.size(), 3u);

    // V1=0 → I=0; V1=1 → I=0.5mA; V1=2 → I=1mA
    EXPECT_NEAR(std::abs(iv[0]), 0.0,   1e-9);
    EXPECT_NEAR(std::abs(iv[1]), 5e-4,  1e-9);
    EXPECT_NEAR(std::abs(iv[2]), 1e-3,  1e-9);
}

// ─────────────────────────────────────────────────────────────────────────────
// 9.  Error: unknown source
// ─────────────────────────────────────────────────────────────────────────────
TEST(DCSweep, UnknownSource) {
    Circuit ckt;
    auto n_in = ckt.node("in");
    ckt.add_device(make_unique<VSource>("V1", n_in, GROUND_INTERNAL, 0.0));
    ckt.add_device(make_unique<Resistor>("R1", n_in, GROUND_INTERNAL, 1000.0));
    ckt.finalize();

    std::vector<DCSweepParam> params;
    DCSweepParam p;
    p.source_name = "VNOTEXIST";
    p.start = 0; p.stop = 1; p.step = 0.5;
    params.push_back(p);

    EXPECT_THROW(solve_dc_sweep(ckt, params), std::invalid_argument);
}

// ─────────────────────────────────────────────────────────────────────────────
// 10. Parser: .dc with bad token count should throw
// ─────────────────────────────────────────────────────────────────────────────
TEST(DCSweep, ParserBadLine) {
    const char* netlist = R"(
V1 in 0 DC 0
R1 in 0 1k
.dc V1 0 5
.end
)";
    NetlistParser parser;
    EXPECT_THROW(parser.parse(netlist), ParseError);
}
