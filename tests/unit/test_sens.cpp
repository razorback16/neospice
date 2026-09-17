#include <gtest/gtest.h>
#include "core/sens.hpp"
#include "core/dc.hpp"
#include "api/neospice.hpp"
#include "parser/netlist_parser.hpp"
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include "devices/resistor.hpp"
#include "framework/ngspice_lib.hpp"
#include <map>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cstdio>
#include <string>

using namespace neospice;
using std::make_unique;

// Helper: find a sensitivity entry by element name
static const SensResult::Entry* find_entry(const SensResult& r,
                                           const std::string& elem) {
    for (auto& e : r.entries)
        if (e.element == elem) return &e;
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// 1. Programmatic API: simple voltage divider
// ─────────────────────────────────────────────────────────────────────────────
TEST(Sens, VoltageDivider) {
    // V1=10, R1=1k, R2=1k  =>  V(out) = 5V
    // dV(out)/dR1 = -V1*R2/(R1+R2)^2 = -10*1000/(2000)^2 = -2.5e-3
    // dV(out)/dR2 =  V1*R1/(R1+R2)^2 =  10*1000/(2000)^2 =  2.5e-3
    // dV(out)/dV1 =  R2/(R1+R2) = 0.5
    Circuit ckt;
    int32_t n_in  = static_cast<int32_t>(ckt.node("in"));
    int32_t n_out = static_cast<int32_t>(ckt.node("out"));
    ckt.add_device(make_unique<VSource>("V1", n_in, GROUND_INTERNAL, 10.0));
    ckt.add_device(make_unique<Resistor>("R1", n_in, n_out, 1000.0));
    ckt.add_device(make_unique<Resistor>("R2", n_out, GROUND_INTERNAL, 1000.0));
    ckt.finalize();

    SensResult sens = solve_sens(ckt, "v(out)");

    EXPECT_NEAR(sens.output_value, 5.0, 1e-6);

    auto* r1 = find_entry(sens, "r1");
    auto* r2 = find_entry(sens, "r2");
    auto* v1 = find_entry(sens, "v1");
    ASSERT_NE(r1, nullptr);
    ASSERT_NE(r2, nullptr);
    ASSERT_NE(v1, nullptr);

    EXPECT_NEAR(r1->sensitivity, -2.5e-3, 1e-6);
    EXPECT_NEAR(r2->sensitivity,  2.5e-3, 1e-6);
    EXPECT_NEAR(v1->sensitivity,  0.5,    1e-6);

    // Normalized sensitivities: sens * param / output
    // R1: -2.5e-3 * 1000 / 5 = -0.5
    // R2:  2.5e-3 * 1000 / 5 =  0.5
    // V1:  0.5    * 10   / 5 =  1.0
    EXPECT_NEAR(r1->normalized, -0.5, 1e-4);
    EXPECT_NEAR(r2->normalized,  0.5, 1e-4);
    EXPECT_NEAR(v1->normalized,  1.0, 1e-4);
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. Unequal divider: asymmetric resistor values
// ─────────────────────────────────────────────────────────────────────────────
TEST(Sens, UnequalDivider) {
    // V1=5, R1=2k, R2=8k  =>  V(out) = 5*8k/10k = 4V
    // dV(out)/dR1 = -V1*R2/(R1+R2)^2 = -5*8000/(10000)^2 = -4e-4
    // dV(out)/dR2 =  V1*R1/(R1+R2)^2 =  5*2000/(10000)^2 =  1e-4
    // dV(out)/dV1 = R2/(R1+R2) = 0.8
    Circuit ckt;
    int32_t n_in  = static_cast<int32_t>(ckt.node("in"));
    int32_t n_out = static_cast<int32_t>(ckt.node("out"));
    ckt.add_device(make_unique<VSource>("V1", n_in, GROUND_INTERNAL, 5.0));
    ckt.add_device(make_unique<Resistor>("R1", n_in, n_out, 2000.0));
    ckt.add_device(make_unique<Resistor>("R2", n_out, GROUND_INTERNAL, 8000.0));
    ckt.finalize();

    SensResult sens = solve_sens(ckt, "v(out)");

    EXPECT_NEAR(sens.output_value, 4.0, 1e-6);

    auto* r1 = find_entry(sens, "r1");
    auto* r2 = find_entry(sens, "r2");
    auto* v1 = find_entry(sens, "v1");
    ASSERT_NE(r1, nullptr);
    ASSERT_NE(r2, nullptr);
    ASSERT_NE(v1, nullptr);

    EXPECT_NEAR(r1->sensitivity, -4e-4, 1e-7);
    EXPECT_NEAR(r2->sensitivity,  1e-4, 1e-7);
    EXPECT_NEAR(v1->sensitivity,  0.8,  1e-6);
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. Current source circuit
// ─────────────────────────────────────────────────────────────────────────────
TEST(Sens, CurrentSource) {
    // I1=1mA from GND to in, R1 from in to out, R2 from out to GND
    // V(out) = I1 * R2 = 0.001 * 2000 = 2V
    // dV(out)/dR1 = 0  (R1 doesn't affect V(out) in series with current source)
    // dV(out)/dR2 = I1 = 0.001  (V/Ohm)
    // dV(out)/dI1 = R2 = 2000   (V/A)
    Circuit ckt;
    int32_t n_in  = static_cast<int32_t>(ckt.node("in"));
    int32_t n_out = static_cast<int32_t>(ckt.node("out"));
    ckt.add_device(make_unique<ISource>("I1", GROUND_INTERNAL, n_in, 0.001));
    ckt.add_device(make_unique<Resistor>("R1", n_in, n_out, 1000.0));
    ckt.add_device(make_unique<Resistor>("R2", n_out, GROUND_INTERNAL, 2000.0));
    ckt.finalize();

    SensResult sens = solve_sens(ckt, "v(out)");

    EXPECT_NEAR(sens.output_value, 2.0, 1e-6);

    auto* r1 = find_entry(sens, "r1");
    auto* r2 = find_entry(sens, "r2");
    auto* i1 = find_entry(sens, "i1");
    ASSERT_NE(r1, nullptr);
    ASSERT_NE(r2, nullptr);
    ASSERT_NE(i1, nullptr);

    EXPECT_NEAR(r1->sensitivity, 0.0,    1e-6);
    EXPECT_NEAR(r2->sensitivity, 0.001,  1e-6);
    EXPECT_NEAR(i1->sensitivity, 2000.0, 1e-2);
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. Parser test: .sens from netlist file
// ─────────────────────────────────────────────────────────────────────────────
TEST(Sens, NetlistParser) {
    Simulator sim;
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/sens_divider.cir";
    auto ckt = sim.load(path);
    auto result = sim.run(ckt);

    ASSERT_TRUE(std::holds_alternative<SensResult>(result.analysis));
    EXPECT_NEAR(std::get<SensResult>(result.analysis).output_value, 5.0, 1e-6);
    EXPECT_EQ(std::get<SensResult>(result.analysis).entries.size(), 3u);

    auto* r1 = find_entry(std::get<SensResult>(result.analysis), "r1");
    auto* r2 = find_entry(std::get<SensResult>(result.analysis), "r2");
    auto* v1 = find_entry(std::get<SensResult>(result.analysis), "v1");
    ASSERT_NE(r1, nullptr);
    ASSERT_NE(r2, nullptr);
    ASSERT_NE(v1, nullptr);

    EXPECT_NEAR(r1->sensitivity, -2.5e-3, 1e-6);
    EXPECT_NEAR(r2->sensitivity,  2.5e-3, 1e-6);
    EXPECT_NEAR(v1->sensitivity,  0.5,    1e-6);
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. Parser test: inline netlist
// ─────────────────────────────────────────────────────────────────────────────
TEST(Sens, InlineNetlist) {
    Simulator sim;
    auto ckt = sim.parse(R"(
Sens inline test
V1 in 0 5
R1 in out 2k
R2 out 0 8k
.sens V(out)
.end
)");
    auto result = sim.run(ckt);

    ASSERT_TRUE(std::holds_alternative<SensResult>(result.analysis));
    EXPECT_NEAR(std::get<SensResult>(result.analysis).output_value, 4.0, 1e-6);
}

// ─────────────────────────────────────────────────────────────────────────────
// 6. Three-resistor chain: V1 -> R1 -> mid -> R2 -> out -> R3 -> GND
// ─────────────────────────────────────────────────────────────────────────────
TEST(Sens, ThreeResistorChain) {
    // V1=12V, R1=1k, R2=2k, R3=3k
    // V(out) = 12 * R3/(R1+R2+R3) = 12 * 3000/6000 = 6V
    // Using the general formula for a divider with two series (R1+R2) and R3:
    // Let Rs = R1+R2 = 3k, so V(out) = V1 * R3/(Rs+R3)
    // dV(out)/dR3 = V1*Rs/(Rs+R3)^2 = 12*3000/(6000)^2 = 1e-3
    // dV(out)/dR1 = -V1*R3/(R1+R2+R3)^2 = -12*3000/(6000)^2 = -1e-3
    // dV(out)/dR2 = -V1*R3/(R1+R2+R3)^2 = -12*3000/(6000)^2 = -1e-3
    // dV(out)/dV1 = R3/(R1+R2+R3) = 0.5
    Circuit ckt;
    int32_t n_in  = static_cast<int32_t>(ckt.node("in"));
    int32_t n_mid = static_cast<int32_t>(ckt.node("mid"));
    int32_t n_out = static_cast<int32_t>(ckt.node("out"));
    ckt.add_device(make_unique<VSource>("V1", n_in, GROUND_INTERNAL, 12.0));
    ckt.add_device(make_unique<Resistor>("R1", n_in, n_mid, 1000.0));
    ckt.add_device(make_unique<Resistor>("R2", n_mid, n_out, 2000.0));
    ckt.add_device(make_unique<Resistor>("R3", n_out, GROUND_INTERNAL, 3000.0));
    ckt.finalize();

    SensResult sens = solve_sens(ckt, "v(out)");

    EXPECT_NEAR(sens.output_value, 6.0, 1e-6);

    auto* r1 = find_entry(sens, "r1");
    auto* r2 = find_entry(sens, "r2");
    auto* r3 = find_entry(sens, "r3");
    auto* v1 = find_entry(sens, "v1");
    ASSERT_NE(r1, nullptr);
    ASSERT_NE(r2, nullptr);
    ASSERT_NE(r3, nullptr);
    ASSERT_NE(v1, nullptr);

    EXPECT_NEAR(r1->sensitivity, -1e-3, 1e-6);
    EXPECT_NEAR(r2->sensitivity, -1e-3, 1e-6);
    EXPECT_NEAR(r3->sensitivity,  1e-3, 1e-6);
    EXPECT_NEAR(v1->sensitivity,  0.5,  1e-6);
}

// ─────────────────────────────────────────────────────────────────────────────
// 7. ngspice comparison
// ─────────────────────────────────────────────────────────────────────────────

TEST(Sens, NgspiceComparison) {
    const std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/sens_divider.cir";
    NgspiceLib ng;
    ng.load_circuit(cir_path);
    ng.run();
    ASSERT_NE(ng.cur_plot(), nullptr);
    const std::string plot = ng.cur_plot();
    ASSERT_TRUE(plot.starts_with("sens"));
    std::map<std::string, double> reference;
    // This entry point supports primary resistance and source DC parameters.
    // ngspice additionally emits model/geometry parameters outside that scope.
    for (const char* name : {"r1", "r2", "v1"}) {
        const auto* vector = ng.get_vec_info(plot + "." + name);
        ASSERT_NE(vector, nullptr) << name;
        ASSERT_EQ(vector->v_length, 1) << name;
        ASSERT_NE(vector->v_realdata, nullptr) << name;
        ASSERT_EQ(vector->v_compdata, nullptr) << name;
        ASSERT_TRUE(std::isfinite(vector->v_realdata[0])) << name;
        reference.emplace(name, vector->v_realdata[0]);
    }
    Simulator sim;
    auto ckt = sim.load(cir_path);
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<SensResult>(result.analysis));

    EXPECT_NEAR(std::get<SensResult>(result.analysis).output_value, 5.0, 1e-6);

    auto* r1 = find_entry(std::get<SensResult>(result.analysis), "r1");
    auto* v1 = find_entry(std::get<SensResult>(result.analysis), "v1");
    ASSERT_NE(r1, nullptr);
    ASSERT_NE(v1, nullptr);

    const auto& sensitivities = std::get<SensResult>(result.analysis);
    ASSERT_TRUE(sensitivities.status.converged);
    ASSERT_EQ(sensitivities.entries.size(), reference.size());
    for (const auto& [name, expected] : reference) {
        const auto* entry = find_entry(sensitivities, name);
        ASSERT_NE(entry, nullptr) << name;
        ASSERT_TRUE(std::isfinite(entry->sensitivity));
        EXPECT_NEAR(entry->sensitivity, expected, 1e-6) << name;
        std::cout << std::setprecision(17) << "DETAIL_SENS|" << name << '|'
                  << expected << '|' << entry->sensitivity << '|'
                  << std::abs(entry->sensitivity - expected) << "|1e-6\n";
    }

    // Analytical values for voltage divider
    EXPECT_NEAR(r1->sensitivity, -2.5e-3, 1e-6);
    EXPECT_NEAR(v1->sensitivity, 0.5, 1e-6);
}

// ─────────────────────────────────────────────────────────────────────────────
// 8. Differential voltage output V(a,b)
// ─────────────────────────────────────────────────────────────────────────────
TEST(Sens, DifferentialOutput) {
    // Wheatstone bridge:
    // V1=10 -> R1(1k) -> a -> R3(3k) -> GND
    //       -> R2(2k) -> b -> R4(4k) -> GND
    // V(a) = V1*R3/(R1+R3) = 10*3/4 = 7.5
    // V(b) = V1*R4/(R2+R4) = 10*4/6 = 20/3
    // V(a,b) = 7.5 - 20/3 = 22.5/3 - 20/3 = 2.5/3 = 5/6

    Simulator sim;
    auto ckt = sim.parse(R"(
Wheatstone bridge sens
V1 in 0 10
R1 in a 1k
R2 in b 2k
R3 a 0 3k
R4 b 0 4k
.sens V(a,b)
.end
)");
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<SensResult>(result.analysis));

    EXPECT_NEAR(std::get<SensResult>(result.analysis).output_value, 5.0 / 6.0, 1e-6);

    // Just verify the output is correct and entries exist
    EXPECT_GE(std::get<SensResult>(result.analysis).entries.size(), 5u);  // 4 resistors + 1 vsource
}

TEST(Sens, FailedBaselineDoesNotProduceSensitivities) {
    Simulator sim;
    auto ckt = sim.parse("Conflicting sources\nV1 out 0 1\nV2 out 0 2\nR1 out 0 1k\n.end\n");
    ckt.options.no_throw = true;
    const auto result = solve_sens(ckt, "v(out)");
    EXPECT_FALSE(result.status.converged);
    EXPECT_TRUE(result.entries.empty());
    EXPECT_EQ(ckt.operating_point(), nullptr);
}

TEST(Sens, FailedPerturbationRestoresParametersInBothErrorModes) {
    // In each circuit the baseline has real roots, but the forward parameter
    // perturbation crosses a fold and the KCL equation has no real solution.
    for (bool no_throw : {false, true}) {
        for (bool resistor_case : {false, true}) {
            SCOPED_TRACE(no_throw);
            SCOPED_TRACE(resistor_case);
            Simulator sim;
            auto ckt = sim.parse(resistor_case
                ? "Resistor fold\nR1 out 0 1\nB1 out 0 I={v(out)^2+0.24999}\n.end\n"
                : "Voltage fold\nV1 in 0 1\nB1 out 0 I={v(out)^2+v(out)+10000*(v(in)-1)}\n.end\n");
            ckt.options.no_throw = no_throw;
            ASSERT_TRUE(solve_dc(ckt).status.converged);
            if (no_throw) {
                const auto result = solve_sens(ckt, "v(out)");
                EXPECT_FALSE(result.status.converged);
                EXPECT_TRUE(result.entries.empty());
            } else {
                EXPECT_THROW(solve_sens(ckt, "v(out)"), SimulationError);
            }
            const auto* device = ckt.devices().front().get();
            if (resistor_case) {
                const auto* r = dynamic_cast<const Resistor*>(device);
                ASSERT_NE(r, nullptr);
                EXPECT_DOUBLE_EQ(r->resistance(), 1.0);
            } else {
                const auto* v = dynamic_cast<const VSource*>(device);
                ASSERT_NE(v, nullptr);
                EXPECT_DOUBLE_EQ(v->dc_value(), 1.0);
            }
            EXPECT_EQ(ckt.operating_point(), nullptr);
            EXPECT_DOUBLE_EQ(ckt.options.diag_gmin, ckt.options.gshunt);
        }
    }
}

TEST(Sens, SuccessfulRunDoesNotLeavePerturbedOperatingPointCached) {
    Simulator sim;
    auto ckt = sim.parse("Divider\nV1 in 0 10\nR1 in out 1k\nR2 out 0 1k\n.end\n");
    ASSERT_TRUE(solve_dc(ckt).status.converged);
    ASSERT_NE(ckt.operating_point(), nullptr);
    const auto baseline = *ckt.operating_point();
    const auto result = solve_sens(ckt, "v(out)");
    ASSERT_TRUE(result.status.converged);
    ASSERT_EQ(result.entries.size(), 3u);
    // A later AC solve may reuse this cache: it must be absent or describe
    // the original circuit, not the final perturbed voltage source.
    if (const auto* cached = ckt.operating_point()) {
        ASSERT_EQ(cached->size(), baseline.size());
        for (size_t i = 0; i < baseline.size(); ++i)
            EXPECT_NEAR((*cached)[i], baseline[i], 1e-13) << i;
    }
}

TEST(Sens, FailedCurrentPerturbationRetainsOnlySuccessfulEntries) {
    for (bool no_throw : {false, true}) {
        SCOPED_TRACE(no_throw);
        Simulator sim;
        // Vsense measures I1 without a resistor whose own perturbation could
        // change the fold. Perturbing Vsense leaves its current unchanged.
        auto ckt = sim.parse("Current fold\nI1 in 0 1\nVsense in 0 0\n"
            "B1 out 0 I={v(out)^2+v(out)+10000*(-i(Vsense)-1)}\n.end\n");
        ckt.options.no_throw = no_throw;
        ASSERT_TRUE(solve_dc(ckt).status.converged);
        if (no_throw) {
            const auto result = solve_sens(ckt, "v(out)");
            EXPECT_FALSE(result.status.converged);
            ASSERT_EQ(result.entries.size(), 1u);
            EXPECT_EQ(result.entries.front().element, "vsense");
            EXPECT_DOUBLE_EQ(result.entries.front().sensitivity, 0.0);
        } else {
            EXPECT_THROW(solve_sens(ckt, "v(out)"), SimulationError);
        }
        const auto* source = dynamic_cast<const ISource*>(ckt.devices().front().get());
        ASSERT_NE(source, nullptr);
        EXPECT_DOUBLE_EQ(source->dc_value(), 1.0);
        EXPECT_EQ(ckt.operating_point(), nullptr);
    }
}
