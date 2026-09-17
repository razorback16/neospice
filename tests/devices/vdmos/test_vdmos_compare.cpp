// VDMOS (DMOS power MOSFET) ngspice comparison suite.
// Tests: DC operating point, N-channel Id-Vds IV curve (DC sweep),
//        P-channel DC operating point.
// Each test runs the same .cir circuit through both ngspice and neospice
// and compares results within engineering tolerances.

#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "framework/ngspice_runner.hpp"
#include "framework/comparator.hpp"
#include "devices/vdmos/vdmos_device.hpp"
#include "parser/netlist_parser.hpp"

#include <algorithm>
#include <cmath>
#include <regex>
#include <string>

using namespace neospice;

class VDMOSValidation : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>();
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

TEST_F(VDMOSValidation, TerminalFormsBindAndMatchNgspice47) {
    const auto path = std::string(TEST_CIRCUITS_DIR) + "/vdmos_terminal_forms.cir";
    auto ckt = sim_.load(path);
    for (const std::string name : {"Mn1", "Mn3", "Mn4", "Mn5",
                                   "Mp1", "Mp3", "Mp4", "Mp5"}) {
        SCOPED_TRACE(name);
        ASSERT_NE(dynamic_cast<const VDMOSDevice*>(ckt.find_device_ptr(name)), nullptr);
    }
    auto expected = ngspice_->run_dc(path);
    std::erase_if(expected.node_voltages, [](const auto& item) {
        return item.first.find('#') != std::string::npos;
    });
    const auto actual = sim_.run_dc(ckt);
    const auto comparison = compare_dc(expected, actual);
    EXPECT_TRUE(comparison.passed) << comparison.worst_signal << " " << comparison.worst_error;
    for (const std::string polarity : {"n", "p"}) {
        const double single = actual.current("vd" + polarity + "1");
        ASSERT_GT(std::abs(single), 1e-3);
        for (const std::string form : {"3", "4", "5"}) {
            SCOPED_TRACE(polarity + form);
            // The optional M=2 must survive each model-name position.
            EXPECT_NEAR(actual.current("vd" + polarity + form), 2 * single,
                        std::abs(single) * 1e-9);
        }
    }
}

// The corpus comparator excludes ngspice's generated v(<inst>#gate) and
// v(<inst>#body_diode) observables. That exclusion is only like-for-like if
// neospice models the same nodes and merely keeps them private, and only
// complete if the reference emits no other generated name for this device.
// Both halves are checked here so the rule cannot drift silently.
TEST_F(VDMOSValidation, GeneratedInternalNodesAreModeledButPrivate) {
    const auto path = std::string(TEST_CIRCUITS_DIR) + "/vdmos_internal_nodes.cir";
    auto ckt = sim_.load(path);
    (void)sim_.run_dc(ckt);  // internal nodes are allocated during setup

    std::vector<std::string> internal;
    for (int32_t i = 0; i < ckt.num_nodes(); ++i) {
        if (ckt.is_internal_node(i)) internal.push_back(ckt.node_name(i));
    }
    std::sort(internal.begin(), internal.end());

    // Only the Rg/Rb device allocates them, and both stay private. The exact
    // names are asserted because docs/kicad-experiment.md quotes them as the
    // neospice counterparts of the two excluded reference observables.
    const std::vector<std::string> expected{"__M1_body diode", "__M1_gate"};
    ASSERT_EQ(internal, expected) << "internal nodes: " << internal.size();
    for (const auto& name : internal) {
        EXPECT_TRUE(ckt.is_internal_node(ckt.node_index(name)));
    }

    // The reference side: every generated name ngspice 47 prints for this deck
    // must be one the documented corpus filter recognizes. A new generated
    // observable fails here instead of becoming an unexplained corpus mismatch.
    const std::regex generated(R"(v\(m[^()#]*#(?:gate|body_diode)\))",
                              std::regex::icase);
    const auto reference = ngspice_->run_dc(path);
    std::vector<std::string> hashed;
    for (const auto& [name, value] : reference.node_voltages) {
        if (name.find('#') != std::string::npos) hashed.push_back(name);
    }
    std::sort(hashed.begin(), hashed.end());
    ASSERT_EQ(hashed.size(), 2u) << "generated reference observables changed";
    for (const auto& name : hashed) {
        EXPECT_TRUE(std::regex_match(name, generated))
            << "unclassified generated observable: " << name;
    }
    // Excluding them loses no operating-point information: the gate node sits
    // at the gate voltage (no DC gate current) and the body node at ~0 V.
    EXPECT_NEAR(reference.node_voltages.at(hashed[1]),
                reference.node_voltages.at("v(g1)"), 1e-12);
    EXPECT_NEAR(reference.node_voltages.at(hashed[0]), 0.0, 1e-12);
}

TEST(VDMOSParser, ThreeTerminalsRequireVdmosModel) {
    NetlistParser parser;
    for (const std::string model : {"NMOS(LEVEL=1)", "PMOS(LEVEL=1)", "D"}) {
        for (const std::string parameters : {"", " M=2", " W=1u L=1u"}) {
            SCOPED_TRACE(model + parameters);
            EXPECT_THROW(parser.parse("Three-terminal non-VDMOS\nM1 d g s QM" +
                parameters + "\n.model QM " + model + "\n.end\n"), ParseError);
        }
    }
}

TEST_F(VDMOSValidation, SubcircuitModelScopeMatchesNgspice47) {
    const auto path = std::string(TEST_CIRCUITS_DIR) + "/vdmos_model_scope.cir";
    auto ckt = sim_.load(path);
    for (const std::string name : {"xn.m1", "xp.m1", "xnested.xinner.m1",
                                   "xlocal.m1", "xshadow.m1", "xn4.m1"}) {
        SCOPED_TRACE(name);
        ASSERT_NE(dynamic_cast<const VDMOSDevice*>(ckt.find_device_ptr(name)), nullptr);
    }
    auto expected = ngspice_->run_dc(path);
    std::erase_if(expected.node_voltages, [](const auto& item) {
        return item.first.find('#') != std::string::npos;
    });
    const auto actual = sim_.run_dc(ckt);
    const auto comparison = compare_dc(expected, actual);
    EXPECT_TRUE(comparison.passed) << comparison.worst_signal << " " << comparison.worst_error;
    const double ordinary = actual.current("vdn");
    ASSERT_GT(std::abs(ordinary), 1e-3);
    EXPECT_NEAR(actual.current("vdnested"), ordinary, 1e-10);
    EXPECT_NEAR(actual.current("vdn4"), ordinary, 1e-10);
    EXPECT_GT(std::abs(actual.current("vdshadow") - ordinary), 1e-3);
}

TEST_F(VDMOSValidation, ModelTemperatureExpressionsFollowRepeatedAnalyses) {
    const auto base = std::string(TEST_CIRCUITS_DIR) + "/vdmos_model_temperature_";
    auto ckt = sim_.load(base + "27.cir");
    double first_current = 0.0;
    for (const int temperature : {27, 0, 60, 27}) {
        SCOPED_TRACE(temperature);
        auto expected = ngspice_->run_dc(base + std::to_string(temperature) + ".cir");
        std::erase_if(expected.node_voltages, [](const auto& item) {
            return item.first.find('#') != std::string::npos;
        });
        ckt.options.temp = temperature + 273.15;
        const auto actual = sim_.run_dc(ckt);
        const auto comparison = compare_dc(expected, actual);
        EXPECT_TRUE(comparison.passed) << comparison.worst_signal << " " << comparison.worst_error;
        EXPECT_GT(std::abs(actual.current("vdglobal")), 1e-4);
        EXPECT_NEAR(actual.current("vdglobal"), actual.current("vdlocal"), 1e-10);
        if (temperature == 27) {
            if (first_current == 0.0) first_current = actual.current("vdglobal");
            else EXPECT_NEAR(actual.current("vdglobal"), first_current, 1e-12);
        }
    }
}

TEST(VDMOSParser, RejectsSixTerminals) {
    NetlistParser parser;
    EXPECT_THROW(parser.parse("Too many VDMOS terminals\n"
        "M1 d g s tj tc extra QM\n.model QM VDMOS\n.end\n"), ParseError);
}

TEST_F(VDMOSValidation, ModelTemperatureAssignmentPrecedenceMatchesNgspice47) {
    struct RestoreReferenceDialect {
        NgspiceRunner& reference;
        ~RestoreReferenceDialect() {
            try { reference.command("unset ngbehavior"); }
            catch (const std::exception& e) { ADD_FAILURE() << e.what(); }
        }
    } restore{*ngspice_};
    for (const bool pspice : {false, true}) {
        SCOPED_TRACE(pspice ? "PSpice AKO" : "default direct assignments");
        ngspice_->command(pspice ? "set ngbehavior=psa" : "unset ngbehavior");
        const auto base = std::string(TEST_CIRCUITS_DIR) +
            (pspice ? "/model_expression_precedence_" : "/model_expression_precedence_direct_");
        auto ckt = sim_.load(base + "27.cir");
        ASSERT_EQ(ckt.options.pspice_compat, pspice);
        for (const int temperature : {27, 0, 60, 27}) {
            SCOPED_TRACE(temperature);
            ckt.options.temp = temperature + 273.15;
            auto expected = ngspice_->run_dc(base + std::to_string(temperature) + ".cir");
            std::erase_if(expected.node_voltages, [](const auto& item) {
                return item.first.find('#') != std::string::npos;
            });
            const auto actual = sim_.run_dc(ckt);
            const auto comparison = compare_dc(expected, actual);
            EXPECT_TRUE(comparison.passed) << comparison.worst_signal << " " << comparison.worst_error;
            EXPECT_GT(std::abs(actual.current("vd0")), 1e-3);
            // Deferred expressions run after literals and in reverse source order.
            // This applies to copied AKO assignments as well as direct duplicates.
            for (const std::string branch : {"vd1", "vd2", "vd3", "vd4"})
                EXPECT_NEAR(actual.current(branch), actual.current("vd0"), 1e-10);
            EXPECT_GT(std::abs(actual.current("vd5")-actual.current("vd0")), 1e-3);
        }
    }
}

TEST_F(VDMOSValidation, MobilityReductionUsesOverdriveNgspice47) {
    const auto path = std::string(TEST_CIRCUITS_DIR) + "/vdmos_theta.cir";
    auto ckt = sim_.load(path);
    auto expected = ngspice_->run_dc(path);
    std::erase_if(expected.node_voltages, [](const auto& item) {
        return item.first.find('#') != std::string::npos;
    });
    const auto actual = sim_.run_dc(ckt);
    const auto comparison = compare_dc(expected, actual);
    EXPECT_TRUE(comparison.passed) << comparison.worst_signal << " " << comparison.worst_error;
    EXPECT_GT(std::abs(actual.current("vdn")), 1e-3);
    EXPECT_NEAR(actual.current("vdp"), -actual.current("vdn"), 1e-10);
    EXPECT_LT(std::abs(actual.current("vdn")), std::abs(actual.current("vdcontrol")));
}

// ---------------------------------------------------------------------------
// 1. N-channel VDMOS DC operating point.
// ---------------------------------------------------------------------------
TEST_F(VDMOSValidation, NmosOperatingPoint) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/vdmos_nmos_dc_op.cir";

    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        FAIL() << "required ngspice not available or failed: " << e.what();
    }
    if (ng_result.node_voltages.empty())
        FAIL() << "required ngspice returned empty DC result (VDMOS may not be compiled in)";

    auto ckt = sim_.load(cir_path);
    DCResult cs_result = sim_.run_dc(ckt);

    auto cmp = compare_dc(ng_result, cs_result, {1e-3, 1e-6});
    EXPECT_TRUE(cmp.passed)
        << "DC OP comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// 2. N-channel VDMOS Id-Vds IV curve (DC sweep of Vds at fixed Vgs).
// ---------------------------------------------------------------------------
TEST_F(VDMOSValidation, NmosIvCurveSweep) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/vdmos_nmos_iv_sweep.cir";

    DCSweepResult ng_result;
    try {
        ng_result = ngspice_->run_dc_sweep(cir_path);
    } catch (const std::exception& e) {
        FAIL() << "required ngspice not available or failed: " << e.what();
    }
    if (ng_result.sweep_values.empty())
        FAIL() << "required ngspice returned empty DC sweep result";

    auto ckt = sim_.load(cir_path);
    DCSweepResult cs_result = sim_.run_dc_sweep(ckt,
        {{DCSweepParam{"Vds", 0.0, 10.0, 0.5}}});

    const auto sweep_error = validate_dc_sweep_data(ng_result, cs_result);
    ASSERT_TRUE(sweep_error.empty()) << sweep_error;

    ASSERT_FALSE(cs_result.sweep_values.empty());
    ASSERT_EQ(ng_result.sweep_values.size(), cs_result.sweep_values.size());
    ASSERT_TRUE(ng_result.currents.count("i(vds)") > 0);
    ASSERT_TRUE(cs_result.currents.count("i(vds)") > 0);

    const auto& ng_ids = ng_result.current("vds");
    const auto& cs_ids = cs_result.current("vds");

    double worst_rel = 0.0;
    for (size_t i = 0; i < ng_result.sweep_values.size(); ++i) {
        double ng_i = ng_ids[i], cs_i = cs_ids[i];
        double denom = std::max(std::fabs(ng_i), 1e-9);
        double rel = std::fabs(cs_i - ng_i) / denom;
        worst_rel = std::max(worst_rel, rel);
    }
    EXPECT_LT(worst_rel, 1e-2)
        << "VDMOS Id-Vds curve deviates from ngspice by " << worst_rel;
}

// ---------------------------------------------------------------------------
// 3. P-channel VDMOS DC operating point (polarity flag vdmosp).
// ---------------------------------------------------------------------------
TEST_F(VDMOSValidation, PmosOperatingPoint) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/vdmos_pmos_dc_op.cir";

    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        FAIL() << "required ngspice not available or failed: " << e.what();
    }
    if (ng_result.node_voltages.empty())
        FAIL() << "required ngspice returned empty DC result";

    auto ckt = sim_.load(cir_path);
    DCResult cs_result = sim_.run_dc(ckt);

    auto cmp = compare_dc(ng_result, cs_result, {1e-3, 1e-6});
    EXPECT_TRUE(cmp.passed)
        << "P-channel DC OP comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;
}

TEST_F(VDMOSValidation, RejectsUnsupportedAC) {
    const std::string path = std::string(TEST_CIRCUITS_DIR) + "/vdmos_nmos_ac.cir";
    const auto reference = ngspice_->run_ac(path);
    ASSERT_TRUE(reference.status.converged);
    ASSERT_EQ(reference.frequency.size(), 7u);
    const auto& output = reference.voltages.at("v(d)");
    ASSERT_EQ(output.size(), reference.frequency.size());
    for (const auto value : output) {
        ASSERT_TRUE(std::isfinite(value.real()));
        ASSERT_TRUE(std::isfinite(value.imag()));
        ASSERT_GT(std::abs(value), 1e-6);
    }
    for (bool no_throw : {false, true}) {
        SCOPED_TRACE(no_throw);
        auto ckt = sim_.load(path);
        ckt.options.no_throw = no_throw;
        try {
            (void)sim_.run(ckt);
            FAIL() << "VDMOS AC must not succeed with an empty device stamp";
        } catch (const SimulationError& error) {
            EXPECT_FALSE(error.status().converged);
            EXPECT_NE(std::string(error.what()).find("VDMOS"), std::string::npos);
            EXPECT_NE(std::string(error.what()).find("not implemented"), std::string::npos);
        }
    }
}

TEST_F(VDMOSValidation, RejectsUnsupportedNoise) {
    const std::string path = std::string(TEST_CIRCUITS_DIR) + "/vdmos_nmos_noise.cir";
    const auto reference = ngspice_->run_noise(path);
    ASSERT_EQ(reference.frequency.size(), 7u);
    ASSERT_EQ(reference.onoise_spectrum.size(), reference.frequency.size());
    ASSERT_EQ(reference.inoise_spectrum.size(), reference.frequency.size());
    for (const auto* spectrum : {&reference.onoise_spectrum, &reference.inoise_spectrum})
        for (double value : *spectrum) {
            ASSERT_TRUE(std::isfinite(value));
            ASSERT_GT(value, 0.0);
        }
    for (bool no_throw : {false, true}) {
        SCOPED_TRACE(no_throw);
        auto ckt = sim_.load(path);
        ckt.options.no_throw = no_throw;
        try {
            (void)sim_.run(ckt);
            FAIL() << "VDMOS noise must not succeed with omitted device contributions";
        } catch (const SimulationError& error) {
            EXPECT_FALSE(error.status().converged);
            EXPECT_NE(std::string(error.what()).find("VDMOS"), std::string::npos);
            EXPECT_NE(std::string(error.what()).find("not implemented"), std::string::npos);
        }
        // Direct callers must also receive an explicit failure from the noise
        // provider, rather than an empty list that means zero device noise.
        bool found = false;
        for (const auto& device : ckt.devices()) {
            if (device->name() != "M1" && device->name() != "m1") continue;
            found = true;
            EXPECT_THROW(device->noise_sources(1e3, {}), SimulationError);
        }
        ASSERT_TRUE(found);
    }
}
