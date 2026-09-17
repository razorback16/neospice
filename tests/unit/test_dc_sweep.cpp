#include <gtest/gtest.h>
#include "core/dc.hpp"
#include "api/neospice.hpp"
#include "parser/netlist_parser.hpp"
#include "devices/vsource.hpp"
#include "devices/resistor.hpp"
#include "output/raw_writer.hpp"
#include "framework/ngspice_runner.hpp"
#include "framework/comparator.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

using namespace neospice;
using std::make_unique;

TEST(DCSweep, PartialFailureRetainsOnlySuccessfulPoints) {
    Simulator sim;
    // KCL gives V(out)^2 + V(out) + V(in) = 0. It has a root at V(in)=0
    // and no real root at V(in)=1. Later sweep points must not be published.
    auto ckt = sim.parse("No real root after first point\nV1 in 0 0.25\n"
        "R1 out 0 1\nB1 out 0 I={V(out)*V(out)+V(in)}\n.end\n");
    ckt.options.no_throw = true;
    auto result = sim.run_dc_sweep(ckt, {{"V1", 0, 2, 1}});
    EXPECT_FALSE(result.status.converged);
    ASSERT_EQ(result.sweep_values, std::vector<double>{0});
    ASSERT_EQ(result.voltage("out").size(), 1u);
    EXPECT_NEAR(result.voltage("out")[0], 0, 1e-12);
    for (const auto& [name, values] : result.voltages) EXPECT_EQ(values.size(), 1u);
    for (const auto& [name, values] : result.currents) EXPECT_EQ(values.size(), 1u);
}

TEST(DCSweep, NestedOrderMatchesNgspice) {
    const std::string path = std::string(TEST_CIRCUITS_DIR) + "/dc_nested.cir";
    Simulator sim;
    NgspiceRunner ngspice;
    auto reference = ngspice.run_dc_sweep(path);
    auto ckt = sim.load(path);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCSweepResult>(result.analysis));
    const auto& actual = std::get<DCSweepResult>(result.analysis);
    ASSERT_TRUE(actual.status.converged);
    ASSERT_EQ(reference.sweep_values, (std::vector<double>{-1, 0, 1, -1, 0, 1}));
    EXPECT_EQ(actual.sweep_values, reference.sweep_values);
    EXPECT_EQ(actual.sweep_var, "v1");
    const auto check_signals = [](const auto& expected, const auto& observed) {
        ASSERT_FALSE(expected.empty());
        for (const auto& [name, values] : expected) {
            auto found = observed.find(name);
            ASSERT_NE(found, observed.end()) << name;
            ASSERT_EQ(values.size(), 6u);
            ASSERT_EQ(found->second.size(), values.size());
            for (size_t i = 0; i < values.size(); ++i)
                EXPECT_NEAR(found->second[i], values[i], 1e-12) << name << " index=" << i;
        }
    };
    check_signals(reference.voltages, actual.voltages);
    check_signals(reference.currents, actual.currents);
}

TEST(DCSweep, DescendingCurrentSourceMatchesNgspice) {
    const std::string path = std::string(TEST_CIRCUITS_DIR) + "/dc_current_sweep.cir";
    Simulator sim;
    NgspiceRunner ngspice;
    auto reference = ngspice.run_dc_sweep(path);
    auto ckt = sim.load(path);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<DCSweepResult>(result.analysis));
    const auto& actual = std::get<DCSweepResult>(result.analysis);
    const auto error = validate_dc_sweep_data(reference, actual);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_EQ(actual.sweep_values, (std::vector<double>{0.001, 0, -0.001}));
    EXPECT_EQ(actual.sweep_var, "i1");
    for (size_t i = 0; i < actual.sweep_values.size(); ++i) {
        EXPECT_NEAR(actual.voltage("in")[i], reference.voltage("in")[i], 1e-12);
        EXPECT_NEAR(actual.voltage("in")[i], -1000*actual.sweep_values[i], 1e-12);
    }
}

TEST(DCSweep, FailureStatusAndSourceRestoreInBothErrorModes) {
    for (bool no_throw : {false, true}) {
        Simulator sim;
        auto ckt = sim.parse("Conflicting sources\nV1 out 0 0.25\n"
                             "V2 out 0 0\nR1 out 0 1k\n.end\n");
        ckt.options.no_throw = no_throw;
        if (no_throw) {
            auto result = sim.run_dc_sweep(ckt, {{"V1", 1, 2, 1}});
            EXPECT_FALSE(result.status.converged);
            EXPECT_TRUE(result.sweep_values.empty());
            for (const auto& [name, values] : result.voltages) EXPECT_TRUE(values.empty());
            for (const auto& [name, values] : result.currents) EXPECT_TRUE(values.empty());
        } else {
            EXPECT_THROW(sim.run_dc_sweep(ckt, {{"V1", 1, 2, 1}}), SimulationError);
        }
        VSource* swept_source = nullptr;
        for (const auto& dev : ckt.devices()) {
            auto* source = dynamic_cast<VSource*>(dev.get());
            if (source && (source->name() == "V1" || source->name() == "v1"))
                swept_source = source;
        }
        ASSERT_NE(swept_source, nullptr);
        EXPECT_DOUBLE_EQ(swept_source->dc_value(), 0.25);
        EXPECT_DOUBLE_EQ(ckt.options.diag_gmin, ckt.options.gshunt);
    }
}

TEST(DCSweep, RejectsInvalidRangesAndUnsupportedDimensions) {
    Simulator sim;
    auto ckt = sim.parse("Range validation\nV1 in 0 1\nR1 in 0 1k\n.end\n");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (const DCSweepParam& parameter : std::vector<DCSweepParam>{
             {"V1", nan, 1, 1}, {"V1", 0, inf, 1}, {"V1", 0, 1, inf},
             {"V1", 0, 1, 0}, {"V1", 0, 1, -1}, {"V1", 1, 0, 1},
             {"V1", 1e20, 2e20, 1}}) {
        EXPECT_THROW(sim.run_dc_sweep(ckt, {parameter}), std::invalid_argument);
    }
    EXPECT_THROW(sim.run_dc_sweep(ckt, {{"V1", 0, 1, 1}, {"V1", 0, 1, 1},
                                      {"V1", 0, 1, 1}}), std::invalid_argument);
    EXPECT_THROW(sim.run_dc_sweep(ckt, {{"V1", 0, 1, 1}, {"v1", 0, 1, 1}}),
                 std::invalid_argument);
}

// ─────────────────────────────────────────────────────────────────────────────
// 1.  Resistor-divider sweep via programmatic API
// ─────────────────────────────────────────────────────────────────────────────
TEST(DCSweep, ResistorDivider) {
    Circuit ckt;
    int32_t n_in  = static_cast<int32_t>(ckt.node("in"));
    int32_t n_out = static_cast<int32_t>(ckt.node("out"));
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
    int32_t n_in  = static_cast<int32_t>(ckt.node("in"));
    int32_t n_out = static_cast<int32_t>(ckt.node("out"));
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

    ASSERT_TRUE(std::holds_alternative<DCSweepResult>(result.analysis));
    auto& sw = std::get<DCSweepResult>(result.analysis);

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
    int32_t n_in1 = static_cast<int32_t>(ckt.node("in1"));
    int32_t n_in2 = static_cast<int32_t>(ckt.node("in2"));
    int32_t n_out = static_cast<int32_t>(ckt.node("out"));
    ckt.add_device(make_unique<VSource>("V1", n_in1, GROUND_INTERNAL, 0.0));
    ckt.add_device(make_unique<VSource>("V2", n_in2, GROUND_INTERNAL, 0.0));
    ckt.add_device(make_unique<Resistor>("R1", n_in1, n_out, 1000.0));
    ckt.add_device(make_unique<Resistor>("R2", n_in2, n_out, 1000.0));
    ckt.add_device(make_unique<Resistor>("R3", n_out, GROUND_INTERNAL, 1000.0));
    ckt.finalize();

    // .dc V1 0 2 1  V2 0 1 0.5
    // inner: V1 = {0, 1, 2}  outer: V2 = {0, 0.5, 1.0}
    // total points = 3 * 3 = 9
    std::vector<DCSweepParam> params;
    DCSweepParam p0;
    p0.source_name = "V1";  p0.start = 0; p0.stop = 2; p0.step = 1;
    DCSweepParam p1;
    p1.source_name = "V2";  p1.start = 0; p1.stop = 1; p1.step = 0.5;
    params.push_back(p0);
    params.push_back(p1);

    DCSweepResult result = solve_dc_sweep(ckt, params);

    // ngspice sweeps the first source fastest, and uses it as the x-axis.
    EXPECT_EQ(result.sweep_var, "v1");
    ASSERT_EQ(result.sweep_values.size(), 9u);

    // Check V(out) for each point
    // Points are ordered: (V1=0,V2=0),(V1=1,V2=0),(V1=2,V2=0),
    //                     (V1=0,V2=0.5),(V1=1,V2=0.5),(V1=2,V2=0.5),
    //                     (V1=0,V2=1),(V1=1,V2=1),(V1=2,V2=1)
    double v1_values[] = {0, 1, 2, 0, 1, 2, 0, 1, 2};
    double v2_values[] = {0, 0, 0, 0.5, 0.5, 0.5, 1, 1, 1};
    for (size_t i = 0; i < 9; ++i) {
        EXPECT_DOUBLE_EQ(result.sweep_values[i], v1_values[i]);
        double expected_vout = (v1_values[i] + v2_values[i]) / 3.0;
        EXPECT_NEAR(result.voltages.at("v(out)")[i], expected_vout, 1e-6)
            << "Failed at point " << i
            << " (V1=" << v1_values[i] << ", V2=" << v2_values[i] << ")";
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

    ASSERT_TRUE(std::holds_alternative<DCSweepResult>(result.analysis));
    auto& sw = std::get<DCSweepResult>(result.analysis);
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

    ASSERT_TRUE(std::holds_alternative<DCSweepResult>(result.analysis));
    auto& sw = std::get<DCSweepResult>(result.analysis);
    EXPECT_EQ(sw.sweep_var, "v1");
    ASSERT_EQ(sw.sweep_values.size(), 9u);

    // Spot-check last point: V1=2, V2=1 → V(out) = 3/3 = 1.0
    EXPECT_NEAR(sw.voltages.at("v(out)")[8], 1.0, 1e-6);
}

// ─────────────────────────────────────────────────────────────────────────────
// 7.  RAW writer: write and verify file is non-empty and well-formed
// ─────────────────────────────────────────────────────────────────────────────
TEST(DCSweep, RawWriterBasic) {
    Circuit ckt;
    int32_t n_in  = static_cast<int32_t>(ckt.node("in"));
    int32_t n_out = static_cast<int32_t>(ckt.node("out"));
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
    int32_t n_top = static_cast<int32_t>(ckt.node("top"));
    int32_t n_mid = static_cast<int32_t>(ckt.node("mid"));
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
    int32_t n_in = static_cast<int32_t>(ckt.node("in"));
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
    EXPECT_NO_THROW(parser.parse(netlist));
}
