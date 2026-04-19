#include <gtest/gtest.h>
#include "devices/coupled_inductor.hpp"
#include "devices/inductor.hpp"
#include "core/matrix.hpp"
#include "core/circuit.hpp"
#include "parser/netlist_parser.hpp"
#include "core/ac.hpp"
#include "core/dc.hpp"
#include "core/transient.hpp"
#include <cmath>

using namespace neospice;

// ---------------------------------------------------------------------------
// Unit tests: CoupledInductor device
// ---------------------------------------------------------------------------

TEST(CoupledInductor, Construction) {
    Inductor l1("L1", 0, 1, 1e-3);
    Inductor l2("L2", 2, 3, 1e-3);
    CoupledInductor k("K1", &l1, &l2, 0.5);
    EXPECT_EQ(k.coupling(), 0.5);
    EXPECT_DOUBLE_EQ(k.mutual_inductance(), 0.5 * std::sqrt(1e-3 * 1e-3));
    EXPECT_DOUBLE_EQ(k.mutual_inductance(), 0.5e-3);
}

TEST(CoupledInductor, MutualInductanceDifferentValues) {
    Inductor l1("L1", 0, 1, 1e-3);
    Inductor l2("L2", 2, 3, 4e-3);
    CoupledInductor k("K1", &l1, &l2, 1.0);
    // M = k * sqrt(L1 * L2) = 1.0 * sqrt(1e-3 * 4e-3) = sqrt(4e-6) = 2e-3
    EXPECT_DOUBLE_EQ(k.mutual_inductance(), 2e-3);
}

TEST(CoupledInductor, InvalidCouplingThrows) {
    Inductor l1("L1", 0, 1, 1e-3);
    Inductor l2("L2", 2, 3, 1e-3);
    EXPECT_THROW(CoupledInductor("K1", &l1, &l2, 1.5), std::invalid_argument);
    EXPECT_THROW(CoupledInductor("K1", &l1, &l2, -1.5), std::invalid_argument);
}

TEST(CoupledInductor, NegativeCouplingAllowed) {
    Inductor l1("L1", 0, 1, 1e-3);
    Inductor l2("L2", 2, 3, 1e-3);
    // Negative coupling should be allowed (dot reversal)
    EXPECT_NO_THROW(CoupledInductor("K1", &l1, &l2, -0.5));
}

TEST(CoupledInductor, StampPattern) {
    // L1: nodes 0,1, branch 4
    // L2: nodes 2,3, branch 5
    // K adds entries at (4,5) and (5,4)
    Inductor l1("L1", 0, 1, 1e-3);
    l1.set_branch_index(4);
    Inductor l2("L2", 2, 3, 1e-3);
    l2.set_branch_index(5);

    CoupledInductor k("K1", &l1, &l2, 0.5);
    SparsityBuilder builder(6);
    l1.stamp_pattern(builder);
    l2.stamp_pattern(builder);
    k.stamp_pattern(builder);
    auto pattern = builder.build();

    // K element adds 2 entries: (br1, br2) and (br2, br1)
    // Each inductor adds 5 entries (np-br, nn-br, br-np, br-nn, br-br)
    // Total: 5 + 5 + 2 = 12
    EXPECT_EQ(pattern.nnz(), 12);
}

TEST(CoupledInductor, ACStamp) {
    // Build a small 6-var system with two inductors and coupling
    Inductor l1("L1", 0, 1, 1e-3);
    l1.set_branch_index(4);
    Inductor l2("L2", 2, 3, 1e-3);
    l2.set_branch_index(5);
    CoupledInductor k("K1", &l1, &l2, 0.5);

    SparsityBuilder builder(6);
    l1.stamp_pattern(builder);
    l2.stamp_pattern(builder);
    k.stamp_pattern(builder);
    auto pattern = builder.build();

    NumericMatrix G(pattern);
    NumericMatrix C(pattern);
    l1.assign_offsets(pattern);
    l2.assign_offsets(pattern);
    k.assign_offsets(pattern);

    std::vector<double> voltages(6, 0.0);
    l1.ac_stamp(voltages, G, C);
    l2.ac_stamp(voltages, G, C);
    k.ac_stamp(voltages, G, C);

    // Check mutual coupling in C matrix
    double M = 0.5 * std::sqrt(1e-3 * 1e-3);  // 0.5e-3
    EXPECT_DOUBLE_EQ(C.value(pattern.offset(4, 5)), -M);  // (br1, br2)
    EXPECT_DOUBLE_EQ(C.value(pattern.offset(5, 4)), -M);  // (br2, br1)

    // Self-inductance in C should still be there
    EXPECT_DOUBLE_EQ(C.value(pattern.offset(4, 4)), -1e-3);  // L1
    EXPECT_DOUBLE_EQ(C.value(pattern.offset(5, 5)), -1e-3);  // L2
}

TEST(CoupledInductor, DCNoStamp) {
    // At DC, K element should not stamp anything
    Inductor l1("L1", 0, 1, 1e-3);
    l1.set_branch_index(4);
    Inductor l2("L2", 2, 3, 1e-3);
    l2.set_branch_index(5);
    CoupledInductor k("K1", &l1, &l2, 0.5);

    SparsityBuilder builder(6);
    l1.stamp_pattern(builder);
    l2.stamp_pattern(builder);
    k.stamp_pattern(builder);
    auto pattern = builder.build();

    NumericMatrix mat(pattern);
    k.assign_offsets(pattern);

    std::vector<double> voltages(6, 0.0);
    std::vector<double> rhs(6, 0.0);
    k.evaluate(voltages, mat, rhs);

    // No transient enabled, so no stamps should be added
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(4, 5)), 0.0);
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(5, 4)), 0.0);
}

TEST(CoupledInductor, TransientStamp) {
    // Verify transient companion model stamps
    Inductor l1("L1", 0, 1, 1e-3);
    l1.set_branch_index(4);
    Inductor l2("L2", 2, 3, 1e-3);
    l2.set_branch_index(5);
    CoupledInductor k("K1", &l1, &l2, 0.5);

    SparsityBuilder builder(6);
    l1.stamp_pattern(builder);
    l2.stamp_pattern(builder);
    k.stamp_pattern(builder);
    auto pattern = builder.build();

    NumericMatrix mat(pattern);
    k.assign_offsets(pattern);

    double dt = 1e-4;
    k.set_transient(dt);

    std::vector<double> voltages(6, 0.0);
    std::vector<double> rhs(6, 0.0);
    k.evaluate(voltages, mat, rhs);

    double M = 0.5e-3;
    double r_eq_m = 2.0 * M / dt;  // Trapezoidal

    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(4, 5)), -r_eq_m);
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(5, 4)), -r_eq_m);
}

// ---------------------------------------------------------------------------
// Parser tests
// ---------------------------------------------------------------------------

TEST(CoupledInductorParser, BasicKElement) {
    const char* netlist = R"(
Test K element
V1 in 0 1.0
L1 in mid 10mH
L2 mid 0 10mH
K1 L1 L2 0.99
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    // Should have 4 devices: V1, L1, L2, K1
    EXPECT_EQ(ckt.devices().size(), 4u);

    // Find the CoupledInductor
    CoupledInductor* ki = nullptr;
    for (auto& dev : ckt.devices()) {
        if (auto* k = dynamic_cast<CoupledInductor*>(dev.get())) {
            ki = k;
            break;
        }
    }
    ASSERT_NE(ki, nullptr);
    EXPECT_DOUBLE_EQ(ki->coupling(), 0.99);
    EXPECT_EQ(ki->inductor1()->name(), "L1");
    EXPECT_EQ(ki->inductor2()->name(), "L2");
}

TEST(CoupledInductorParser, UnknownInductorThrows) {
    const char* netlist = R"(
Test K element bad ref
L1 1 0 10mH
K1 L1 L_nonexistent 0.5
.op
.end
)";
    NetlistParser parser;
    EXPECT_THROW(parser.parse(netlist), ParseError);
}

TEST(CoupledInductorParser, CaseInsensitive) {
    const char* netlist = R"(
Test K case insensitive
V1 in 0 1.0
l1 in mid 10mH
l2 mid 0 10mH
k1 l1 l2 0.5
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    CoupledInductor* ki = nullptr;
    for (auto& dev : ckt.devices()) {
        if (auto* k = dynamic_cast<CoupledInductor*>(dev.get())) {
            ki = k;
            break;
        }
    }
    ASSERT_NE(ki, nullptr);
    EXPECT_DOUBLE_EQ(ki->coupling(), 0.5);
}

// ---------------------------------------------------------------------------
// AC analysis: ideal transformer (k=1)
// ---------------------------------------------------------------------------

TEST(CoupledInductorAC, IdealTransformerVoltageRatio) {
    // Ideal transformer: V2/V1 = sqrt(L2/L1) = N2/N1
    // L1 = 1mH, L2 = 4mH → N2/N1 = 2 → V2 should be ~2*V1
    //
    // Circuit:
    //   V1 (AC=1) drives primary winding L1 through a small resistance.
    //   L2 is loaded with a resistor.
    //
    //         R1=1     L1=1mH
    //   V1 --/\/\/--+--LLLL--+-- GND
    //               |        |
    //               | K=0.999|
    //               |        |
    //   Vout -------+--LLLL--+-- GND
    //               |  L2=4mH|
    //              [R2=1k]
    //               |
    //              GND
    //
    // At frequency f, the transformer couples:
    // V_secondary / V_primary ≈ sqrt(L2/L1) = 2
    const char* netlist = R"(
Ideal transformer AC test
V1 in 0 DC 0 AC 1
R1 in pri 1
L1 pri 0 1mH
L2 sec 0 4mH
K1 L1 L2 0.999
R2 sec 0 1k
.ac dec 1 1k 1k
.end
)";

    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    auto result = solve_ac(ckt, AnalysisCommand::DEC, 1, 1e3, 1e3);
    ASSERT_EQ(result.frequency.size(), 1u);

    auto it_sec = result.voltages.find("v(sec)");
    ASSERT_NE(it_sec, result.voltages.end());
    double v_sec_mag = std::abs(it_sec->second[0]);

    // With k=0.999, high-frequency, large load resistance:
    // The voltage ratio should be approximately sqrt(L2/L1) = 2
    // Allow some deviation since k < 1 and there's a source resistance
    EXPECT_NEAR(v_sec_mag, 2.0, 0.3);
}

TEST(CoupledInductorAC, LooseCouplingLowerGain) {
    // Same circuit but with k=0.5 — gain should be much lower than k=0.999
    const char* netlist_tight = R"(
Tight coupling
V1 in 0 DC 0 AC 1
R1 in pri 1
L1 pri 0 1mH
L2 sec 0 4mH
K1 L1 L2 0.999
R2 sec 0 1k
.ac dec 1 10k 10k
.end
)";

    const char* netlist_loose = R"(
Loose coupling
V1 in 0 DC 0 AC 1
R1 in pri 1
L1 pri 0 1mH
L2 sec 0 4mH
K1 L1 L2 0.5
R2 sec 0 1k
.ac dec 1 10k 10k
.end
)";

    NetlistParser parser;
    auto ckt_tight = parser.parse(netlist_tight);
    auto ckt_loose = parser.parse(netlist_loose);

    auto result_tight = solve_ac(ckt_tight, AnalysisCommand::DEC, 1, 1e4, 1e4);
    auto result_loose = solve_ac(ckt_loose, AnalysisCommand::DEC, 1, 1e4, 1e4);

    double v_tight = std::abs(result_tight.voltages.at("v(sec)")[0]);
    double v_loose = std::abs(result_loose.voltages.at("v(sec)")[0]);

    // Tight coupling should give higher secondary voltage than loose coupling
    EXPECT_GT(v_tight, v_loose);
}

// ---------------------------------------------------------------------------
// AC analysis: verify mutual inductance impedance
// ---------------------------------------------------------------------------

TEST(CoupledInductorAC, MutualImpedanceValue) {
    // Two identical inductors L=1mH with k=0.5 → M = 0.5mH
    // At f=1kHz, ω = 2π*1000
    // jωM = j * 2π*1000 * 0.5e-3 = j * π ≈ j*3.14159
    //
    // Simple test: two inductors in series-aiding configuration
    // L_total = L1 + L2 + 2M (series aiding)
    // With L1=L2=1mH, M=0.5mH: L_total = 3mH
    //
    // Circuit: V1 AC=1 -> L1 -> L2 -> R -> GND, with K coupling L1 & L2
    // At frequency f, impedance = R + jω*(L1+L2+2M)
    //
    // But we need to be careful: the SPICE coupling adds the M terms
    // automatically, and the total impedance seen is as if the
    // inductors have an additional mutual coupling.
    //
    // Let's just verify that the current magnitude changes correctly.
    // Without coupling: |Z| = sqrt(R^2 + (ω*(L1+L2))^2)
    // With coupling k=0.5: |Z| depends on dot orientation.
    //
    // Use a reference circuit without coupling and verify the difference.

    const char* netlist_uncoupled = R"(
Uncoupled inductors in series
V1 in 0 DC 0 AC 1
L1 in mid 1mH
L2 mid out 1mH
R1 out 0 100
.ac dec 1 1k 1k
.end
)";

    const char* netlist_coupled = R"(
Coupled inductors in series
V1 in 0 DC 0 AC 1
L1 in mid 1mH
L2 mid out 1mH
K1 L1 L2 0.5
R1 out 0 100
.ac dec 1 1k 1k
.end
)";

    NetlistParser parser;
    auto ckt_unc = parser.parse(netlist_uncoupled);
    auto ckt_cpl = parser.parse(netlist_coupled);

    auto res_unc = solve_ac(ckt_unc, AnalysisCommand::DEC, 1, 1e3, 1e3);
    auto res_cpl = solve_ac(ckt_cpl, AnalysisCommand::DEC, 1, 1e3, 1e3);

    // Current through V1
    auto i_unc = std::abs(res_unc.currents.at("i(v1)")[0]);
    auto i_cpl = std::abs(res_cpl.currents.at("i(v1)")[0]);

    // Series aiding: L_eff = L1 + L2 + 2M = 2mH + 2*0.5mH = 3mH
    // Higher inductance → higher impedance → lower current
    EXPECT_LT(i_cpl, i_unc);

    // Verify ratio approximately
    double omega = 2.0 * M_PI * 1e3;
    double z_unc = std::sqrt(100.0*100.0 + std::pow(omega * 2e-3, 2));
    double z_cpl = std::sqrt(100.0*100.0 + std::pow(omega * 3e-3, 2));
    double expected_ratio = z_unc / z_cpl;  // i_cpl/i_unc * (1/1) since V=1
    double actual_ratio = i_cpl / i_unc;

    EXPECT_NEAR(actual_ratio, expected_ratio, 0.05);
}

// ---------------------------------------------------------------------------
// DC test: coupled inductors are transparent at DC
// ---------------------------------------------------------------------------

TEST(CoupledInductorDC, ShortCircuitAtDC) {
    // Both inductors are short circuits at DC, K element adds nothing
    const char* netlist = R"(
DC test with coupled inductors
V1 in 0 5
L1 in mid 10mH
L2 mid out 10mH
K1 L1 L2 0.99
R1 out 0 100
.op
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);

    auto result = solve_dc(ckt);
    // Current = 5V / 100Ω = 50mA (inductors are short at DC)
    EXPECT_NEAR(result.node_voltages.at("v(out)"), 5.0, 1e-6);
    EXPECT_NEAR(result.branch_currents.at("i(v1)"), -0.05, 1e-6);
}

// ---------------------------------------------------------------------------
// Transient companion model unit tests
// ---------------------------------------------------------------------------

TEST(CoupledInductorTransient, TrapezoidalCompanionModel) {
    // Verify the trapezoidal companion model produces correct matrix and RHS
    // after setting state manually
    Inductor l1("L1", 0, 1, 1e-3);
    l1.set_branch_index(4);
    Inductor l2("L2", 2, 3, 1e-3);
    l2.set_branch_index(5);
    CoupledInductor k("K1", &l1, &l2, 0.5);

    SparsityBuilder builder(6);
    l1.stamp_pattern(builder);
    l2.stamp_pattern(builder);
    k.stamp_pattern(builder);
    auto pattern = builder.build();
    k.assign_offsets(pattern);

    double dt = 1e-4;
    double M = 0.5e-3;
    double r_eq_m = 2.0 * M / dt;

    // Set up transient with trapezoidal (method=0)
    k.set_transient(dt);
    k.set_integration_method(0);

    // After init: all history = 0
    std::vector<double> sol(6, 0.0);
    k.init_dc_state(sol);

    // First evaluate: v_eq = r_eq_m*0 + 0 = 0
    {
        NumericMatrix mat(pattern);
        std::vector<double> rhs(6, 0.0);
        k.evaluate(sol, mat, rhs);

        EXPECT_DOUBLE_EQ(mat.value(pattern.offset(4, 5)), -r_eq_m);
        EXPECT_DOUBLE_EQ(mat.value(pattern.offset(5, 4)), -r_eq_m);
        EXPECT_DOUBLE_EQ(rhs[4], 0.0);  // v_eq_12 = r_eq_m * 0 + 0
        EXPECT_DOUBLE_EQ(rhs[5], 0.0);  // v_eq_21 = r_eq_m * 0 + 0
    }

    // Simulate accepting a step with some branch currents
    sol[4] = 0.1;  // I_L1 = 0.1A
    sol[5] = 0.05; // I_L2 = 0.05A
    k.accept_step_from_solution(sol);

    // Second evaluate: history terms should appear in RHS
    {
        NumericMatrix mat(pattern);
        std::vector<double> rhs(6, 0.0);
        k.evaluate(sol, mat, rhs);

        // v_m12_prev = r_eq_m * (0.05 - 0) - 0 = r_eq_m * 0.05
        // v_m21_prev = r_eq_m * (0.1 - 0) - 0 = r_eq_m * 0.1
        double v_m12 = r_eq_m * 0.05;
        double v_m21 = r_eq_m * 0.1;

        double expected_rhs_4 = r_eq_m * 0.05 + v_m12;  // r_eq_m * i2_prev + v_m12
        double expected_rhs_5 = r_eq_m * 0.1  + v_m21;  // r_eq_m * i1_prev + v_m21

        EXPECT_NEAR(rhs[4], expected_rhs_4, 1e-12);
        EXPECT_NEAR(rhs[5], expected_rhs_5, 1e-12);
    }
}

TEST(CoupledInductorTransient, GearCompanionModel) {
    // Verify Gear-2 companion model
    Inductor l1("L1", 0, 1, 1e-3);
    l1.set_branch_index(4);
    Inductor l2("L2", 2, 3, 1e-3);
    l2.set_branch_index(5);
    CoupledInductor k("K1", &l1, &l2, 0.5);

    SparsityBuilder builder(6);
    l1.stamp_pattern(builder);
    l2.stamp_pattern(builder);
    k.stamp_pattern(builder);
    auto pattern = builder.build();
    k.assign_offsets(pattern);

    double dt = 1e-4;
    double M = 0.5e-3;

    // Set up transient with Gear (method=1)
    k.set_transient(dt);
    k.set_integration_method(1);

    std::vector<double> sol(6, 0.0);
    k.init_dc_state(sol);

    // Accept first step (transitions to gear_ready)
    sol[4] = 0.1;  // I_L1
    sol[5] = 0.05; // I_L2
    k.accept_step_from_solution(sol);

    // Accept second step
    sol[4] = 0.15;
    sol[5] = 0.08;
    k.accept_step_from_solution(sol);

    // Now i1_prev=0.15, i2_prev=0.08, i1_prev2=0.1, i2_prev2=0.05
    // Gear-2 companion:
    double r_eq_m_gear = 1.5 * M / dt;
    double v_eq_12_gear = (M / (2.0 * dt)) * (4.0 * 0.08 - 0.05);
    double v_eq_21_gear = (M / (2.0 * dt)) * (4.0 * 0.15 - 0.1);

    {
        NumericMatrix mat(pattern);
        std::vector<double> rhs(6, 0.0);
        k.evaluate(sol, mat, rhs);

        EXPECT_DOUBLE_EQ(mat.value(pattern.offset(4, 5)), -r_eq_m_gear);
        EXPECT_DOUBLE_EQ(mat.value(pattern.offset(5, 4)), -r_eq_m_gear);
        EXPECT_NEAR(rhs[4], v_eq_12_gear, 1e-12);
        EXPECT_NEAR(rhs[5], v_eq_21_gear, 1e-12);
    }
}

TEST(CoupledInductorTransient, ZeroCouplingNoStamp) {
    // With k=0, mutual = 0, so no transient stamps should appear
    Inductor l1("L1", 0, 1, 1e-3);
    l1.set_branch_index(4);
    Inductor l2("L2", 2, 3, 1e-3);
    l2.set_branch_index(5);
    CoupledInductor k("K1", &l1, &l2, 0.0);

    EXPECT_DOUBLE_EQ(k.mutual_inductance(), 0.0);

    SparsityBuilder builder(6);
    l1.stamp_pattern(builder);
    l2.stamp_pattern(builder);
    k.stamp_pattern(builder);
    auto pattern = builder.build();
    k.assign_offsets(pattern);

    k.set_transient(1e-4);

    std::vector<double> sol(6, 0.0);
    k.init_dc_state(sol);

    NumericMatrix mat(pattern);
    std::vector<double> rhs(6, 0.0);
    k.evaluate(sol, mat, rhs);

    // With M=0, no coupling stamps
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(4, 5)), 0.0);
    EXPECT_DOUBLE_EQ(mat.value(pattern.offset(5, 4)), 0.0);
    EXPECT_DOUBLE_EQ(rhs[4], 0.0);
    EXPECT_DOUBLE_EQ(rhs[5], 0.0);
}
