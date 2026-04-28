#include <gtest/gtest.h>
#include "core/tf.hpp"
#include "core/dc.hpp"
#include "api/neospice.hpp"
#include "parser/netlist_parser.hpp"
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include "devices/resistor.hpp"
#include <cmath>
#include <cstdio>
#include <regex>
#include <string>

using namespace neospice;
using std::make_unique;

// ─────────────────────────────────────────────────────────────────────────────
// 1.  Programmatic API: resistive voltage divider with VSource input
// ─────────────────────────────────────────────────────────────────────────────
TEST(TF, ResistiveDividerVSource) {
    // Circuit: V1(1V) → R1(1k) → mid → R2(2k) → out → R3(10k) → GND
    // .tf V(out) V1
    Circuit ckt;
    auto n_in  = ckt.node("in");
    auto n_mid = ckt.node("mid");
    auto n_out = ckt.node("out");
    ckt.add_device(make_unique<VSource>("V1", n_in, GROUND_INTERNAL, 1.0));
    ckt.add_device(make_unique<Resistor>("R1", n_in, n_mid, 1000.0));
    ckt.add_device(make_unique<Resistor>("R2", n_mid, n_out, 2000.0));
    ckt.add_device(make_unique<Resistor>("R3", n_out, GROUND_INTERNAL, 10000.0));
    ckt.finalize();

    TFResult tf = solve_tf(ckt, "v(out)", "v1");

    // Analytical:
    // TF = R3 / (R1+R2+R3) = 10000/13000 = 0.769231
    // Zin = R1+R2+R3 = 13000
    // Zout = R3 || (R1+R2) = (10000*3000) / 13000 = 2307.692
    EXPECT_NEAR(tf.transfer_function, 10000.0 / 13000.0, 1e-6);
    EXPECT_NEAR(tf.input_impedance, 13000.0, 1e-3);
    EXPECT_NEAR(tf.output_impedance, 30000.0 / 13.0, 1e-3);
}

// ─────────────────────────────────────────────────────────────────────────────
// 2.  Simple voltage divider: V(out)/V1 with two resistors
// ─────────────────────────────────────────────────────────────────────────────
TEST(TF, SimpleVoltageDivider) {
    Circuit ckt;
    auto n_in  = ckt.node("in");
    auto n_out = ckt.node("out");
    ckt.add_device(make_unique<VSource>("V1", n_in, GROUND_INTERNAL, 5.0));
    ckt.add_device(make_unique<Resistor>("R1", n_in, n_out, 1000.0));
    ckt.add_device(make_unique<Resistor>("R2", n_out, GROUND_INTERNAL, 1000.0));
    ckt.finalize();

    TFResult tf = solve_tf(ckt, "v(out)", "v1");

    // TF = R2/(R1+R2) = 0.5
    // Zin = R1+R2 = 2000
    // Zout = R1||R2 = 500
    EXPECT_NEAR(tf.transfer_function, 0.5, 1e-6);
    EXPECT_NEAR(tf.input_impedance, 2000.0, 1e-3);
    EXPECT_NEAR(tf.output_impedance, 500.0, 1e-3);
}

// ─────────────────────────────────────────────────────────────────────────────
// 3.  ISource input: V(out)/I1
// ─────────────────────────────────────────────────────────────────────────────
TEST(TF, CurrentSourceInput) {
    // I1 (1mA) from GND to in, R1 from in to out, R2 from out to GND
    Circuit ckt;
    auto n_in  = ckt.node("in");
    auto n_out = ckt.node("out");
    // ISource: current flows from np to nn through the source.
    // np=GND, nn=in means current enters node "in".
    ckt.add_device(make_unique<ISource>("I1", GROUND_INTERNAL, n_in, 0.001));
    ckt.add_device(make_unique<Resistor>("R1", n_in, n_out, 1000.0));
    ckt.add_device(make_unique<Resistor>("R2", n_out, GROUND_INTERNAL, 2000.0));
    ckt.finalize();

    TFResult tf = solve_tf(ckt, "v(out)", "i1");

    // With unit current injection at I1's nodes:
    // V(out) = I * R2 = 1 * 2000 = 2000 (transimpedance)
    // Zin = V(in) / I(in) = (R1+R2) = 3000
    // Zout = R1 || R2 (shunting at output) ... wait, for ISource input:
    //   ngspice: rhs[node1] -= 1; rhs[node2] += 1 (inject 1A from node1 to node2)
    //   ISource nodes: GND and "in"
    //   So rhs[GND] -= 1 (no-op), rhs[in] += 1
    //   Solving: V(in) = 1 * (R1+R2) = 3000, V(out) = 1 * R2 = 2000
    //   TF = V(out) = 2000 (transimpedance in Ohms)
    //   Zin = V(nn) - V(np) = V(in) - V(GND) = 3000
    //   Zout: inject unit current at output.  ISource is open (no branch variable),
    //   so looking into "out" we see R2 to GND and R1 to "in" (floating since I1
    //   has no path to ground other than through R1/R2).  Actually I1 connects
    //   GND and "in", so it provides a DC path — but in the small-signal Jacobian
    //   it's just a current source (open circuit).  So Zout = R2 || (R1 + infinity)
    //   ≈ R2 = 2000.  Wait — actually R1 connects "in" to "out", and "in" connects
    //   to GND via the ISource path.  But in the MNA matrix the ISource has NO
    //   matrix stamps at all (only RHS).  So for the Zout solve, injecting current
    //   at "out": the path is R2 to GND and R1 to "in", and "in" is only connected
    //   to GND via gmin.  With gmin≈0 effectively, Zout ≈ R2 = 2000.
    //   Confirmed by ngspice: output_impedance = 2000.
    EXPECT_NEAR(tf.transfer_function, 2000.0, 1e-3);
    EXPECT_NEAR(tf.input_impedance, 3000.0, 1e-3);
    EXPECT_NEAR(tf.output_impedance, 2000.0, 1e-1);
}

// ─────────────────────────────────────────────────────────────────────────────
// 4.  Current output: I(V2)/V1
// ─────────────────────────────────────────────────────────────────────────────
TEST(TF, CurrentOutput) {
    // V1 → R1 → node_mid → V2 → GND (V2=0V, ammeter)
    Circuit ckt;
    auto n_in  = ckt.node("in");
    auto n_mid = ckt.node("mid");
    ckt.add_device(make_unique<VSource>("V1", n_in, GROUND_INTERNAL, 5.0));
    ckt.add_device(make_unique<Resistor>("R1", n_in, n_mid, 1000.0));
    ckt.add_device(make_unique<VSource>("V2", n_mid, GROUND_INTERNAL, 0.0));
    ckt.finalize();

    TFResult tf = solve_tf(ckt, "i(v2)", "v1");

    // I(V2) = V1 / R1, so TF = 1/R1 = 0.001
    // Zin: with unit V on V1, current through V1 = V1/R1 = 1/1000
    //   so Zin = -1/(I through V1) = ... need careful analysis
    //   Actually V1 branch current: V(in) = solution value at V1
    //   With unit excitation at V1 branch: V(in)=1, V(mid)=0, I(R1)=1/1000
    //   I(V1) = -1/1000 (current exits V1), so rhs1[V1_branch] = -1/1000
    //   Zin = -1/(-1/1000) = 1000
    EXPECT_NEAR(tf.transfer_function, 1.0 / 1000.0, 1e-9);
    EXPECT_NEAR(tf.input_impedance, 1000.0, 1e-3);
}

// ─────────────────────────────────────────────────────────────────────────────
// 5.  Parser test: .tf from netlist file
// ─────────────────────────────────────────────────────────────────────────────
TEST(TF, NetlistParser) {
    Simulator sim;
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/tf_resistive_divider.cir";
    auto ckt = sim.load(path);
    auto result = sim.run(ckt);

    ASSERT_TRUE(std::holds_alternative<TFResult>(result.analysis));

    // Same circuit as ResistiveDividerVSource
    EXPECT_NEAR(std::get<TFResult>(result.analysis).transfer_function, 10000.0 / 13000.0, 1e-6);
    EXPECT_NEAR(std::get<TFResult>(result.analysis).input_impedance, 13000.0, 1e-3);
    EXPECT_NEAR(std::get<TFResult>(result.analysis).output_impedance, 30000.0 / 13.0, 1e-3);
}

// ─────────────────────────────────────────────────────────────────────────────
// 6.  Parser test: .tf from inline netlist
// ─────────────────────────────────────────────────────────────────────────────
TEST(TF, InlineNetlist) {
    Simulator sim;
    auto ckt = sim.parse(R"(
TF inline test
V1 in 0 1
R1 in out 1k
R2 out 0 1k
.tf V(out) V1
.end
)");
    auto result = sim.run(ckt);

    ASSERT_TRUE(std::holds_alternative<TFResult>(result.analysis));
    EXPECT_NEAR(std::get<TFResult>(result.analysis).transfer_function, 0.5, 1e-6);
    EXPECT_NEAR(std::get<TFResult>(result.analysis).input_impedance, 2000.0, 1e-3);
    EXPECT_NEAR(std::get<TFResult>(result.analysis).output_impedance, 500.0, 1e-3);
}

// ─────────────────────────────────────────────────────────────────────────────
// 7.  ngspice comparison: run ngspice and parse .tf output
// ─────────────────────────────────────────────────────────────────────────────
static bool parse_ngspice_tf_output(const std::string& output,
                                    double& tf_val, double& zin, double& zout) {
    // Parse ngspice text output:
    //   transfer_function = 7.692308e-01
    //   output_impedance_at_v(out) = 2.307692e+03
    //   v1#input_impedance = 1.300000e+04
    std::regex tf_re(R"(transfer_function\s*=\s*([eE0-9.+-]+))");
    std::regex zin_re(R"(input_impedance\s*=\s*([eE0-9.+-]+))");
    std::regex zout_re(R"(output_impedance\S*\s*=\s*([eE0-9.+-]+))");

    std::smatch match;
    bool got_tf = false, got_zin = false, got_zout = false;

    if (std::regex_search(output, match, tf_re)) {
        tf_val = std::stod(match[1]);
        got_tf = true;
    }
    if (std::regex_search(output, match, zin_re)) {
        zin = std::stod(match[1]);
        got_zin = true;
    }
    if (std::regex_search(output, match, zout_re)) {
        zout = std::stod(match[1]);
        got_zout = true;
    }
    return got_tf && got_zin && got_zout;
}

TEST(TF, NgspiceComparison) {
    // Run ngspice on the test circuit
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/tf_resistive_divider.cir";
    std::string cmd = std::string("/usr/bin/ngspice") + " -b " + cir_path + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    ASSERT_NE(pipe, nullptr) << "Failed to run ngspice";

    char buffer[256];
    std::string ng_output;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        ng_output += buffer;
    }
    pclose(pipe);

    double ng_tf = 0, ng_zin = 0, ng_zout = 0;
    ASSERT_TRUE(parse_ngspice_tf_output(ng_output, ng_tf, ng_zin, ng_zout))
        << "Failed to parse ngspice output:\n" << ng_output;

    // Run neospice
    Simulator sim;
    auto ckt = sim.load(cir_path);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TFResult>(result.analysis));

    // Compare (tight tolerance — both are direct solvers)
    EXPECT_NEAR(std::get<TFResult>(result.analysis).transfer_function, ng_tf, 1e-6)
        << "TF mismatch: neospice=" << std::get<TFResult>(result.analysis).transfer_function
        << " ngspice=" << ng_tf;
    EXPECT_NEAR(std::get<TFResult>(result.analysis).input_impedance, ng_zin, 1e-3)
        << "Zin mismatch: neospice=" << std::get<TFResult>(result.analysis).input_impedance
        << " ngspice=" << ng_zin;
    EXPECT_NEAR(std::get<TFResult>(result.analysis).output_impedance, ng_zout, 1e-3)
        << "Zout mismatch: neospice=" << std::get<TFResult>(result.analysis).output_impedance
        << " ngspice=" << ng_zout;
}

// ─────────────────────────────────────────────────────────────────────────────
// 8.  Wheatstone bridge — two-node differential output V(a,b)
// ─────────────────────────────────────────────────────────────────────────────
TEST(TF, DifferentialOutput) {
    Simulator sim;
    auto ckt = sim.parse(R"(
Wheatstone bridge TF
V1 in 0 1
R1 in a 1k
R2 in b 2k
R3 a 0 3k
R4 b 0 4k
.tf V(a,b) V1
.end
)");
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TFResult>(result.analysis));

    // V(a) = V1 * R3/(R1+R3) = 3/4
    // V(b) = V1 * R4/(R2+R4) = 4/6 = 2/3
    // TF = V(a) - V(b) = 3/4 - 2/3 = 9/12 - 8/12 = 1/12
    EXPECT_NEAR(std::get<TFResult>(result.analysis).transfer_function, 1.0 / 12.0, 1e-6);
}

// ─────────────────────────────────────────────────────────────────────────────
// 9.  Additional ngspice comparison with a more complex circuit
// ─────────────────────────────────────────────────────────────────────────────
TEST(TF, NgspiceComparisonCurrentSource) {
    // Create a temp circuit file for ISource TF
    std::string cir_text = R"(Current source TF test
I1 0 in 1m
R1 in out 1k
R2 out 0 2k
.tf V(out) I1
.end
)";
    // Write to temp file
    std::string tmp_path = "/tmp/neospice_tf_isrc_test.cir";
    {
        FILE* f = fopen(tmp_path.c_str(), "w");
        ASSERT_NE(f, nullptr);
        fputs(cir_text.c_str(), f);
        fclose(f);
    }

    // Run ngspice
    std::string cmd = std::string("/usr/bin/ngspice") + " -b " + tmp_path + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    ASSERT_NE(pipe, nullptr);
    char buffer[256];
    std::string ng_output;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        ng_output += buffer;
    }
    pclose(pipe);

    double ng_tf = 0, ng_zin = 0, ng_zout = 0;
    ASSERT_TRUE(parse_ngspice_tf_output(ng_output, ng_tf, ng_zin, ng_zout))
        << "Failed to parse ngspice output:\n" << ng_output;

    // Run neospice
    Simulator sim;
    auto ckt = sim.parse(cir_text);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TFResult>(result.analysis));

    EXPECT_NEAR(std::get<TFResult>(result.analysis).transfer_function, ng_tf, 1e-3)
        << "TF mismatch: neospice=" << std::get<TFResult>(result.analysis).transfer_function
        << " ngspice=" << ng_tf;
    EXPECT_NEAR(std::get<TFResult>(result.analysis).input_impedance, ng_zin, 1e-1)
        << "Zin mismatch: neospice=" << std::get<TFResult>(result.analysis).input_impedance
        << " ngspice=" << ng_zin;
    EXPECT_NEAR(std::get<TFResult>(result.analysis).output_impedance, ng_zout, 1e-1)
        << "Zout mismatch: neospice=" << std::get<TFResult>(result.analysis).output_impedance
        << " ngspice=" << ng_zout;

    std::remove(tmp_path.c_str());
}
