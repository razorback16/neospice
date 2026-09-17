// MES ngspice comparison suite.
// Tests: DC operating point, AC small-signal, noise spectra.

#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "framework/ngspice_runner.hpp"
#include "framework/comparator.hpp"
#include "devices/device_registry.hpp"
#include "parser/model_cards.hpp"
#include <array>

#include <cmath>
#include <complex>
#include <map>
#include <string>
#include <algorithm>

using namespace neospice;

// ============================================================================
// Test fixture
// ============================================================================

class MesValidation : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>();
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// ============================================================================
// DC Operating Point
// ============================================================================

TEST_F(MesValidation, DcOpNmfCommonSource) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/mes_dc_op.cir";

    DCResult ng_result;
    try {
        ng_result = ngspice_->run_dc(cir_path);
    } catch (const std::exception& e) {
        FAIL() << "required ngspice not available or failed: " << e.what();
    }

    if (ng_result.node_voltages.empty()) {
        FAIL() << "required ngspice returned empty DC result (MES may not be compiled in)";
    }

    auto ckt = sim_.load(cir_path);
    DCResult cs_result = sim_.run_dc(ckt);

    auto cmp = compare_dc(ng_result, cs_result, {2e-13, 1e-6});
    EXPECT_TRUE(cmp.passed)
        << "DC OP comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;
}

// ============================================================================
// AC Small-Signal
// ============================================================================

TEST_F(MesValidation, AcNmfCommonSource) {
    std::string cir_path = std::string(TEST_CIRCUITS_DIR) + "/mes_ac.cir";

    ACResult ng_result;
    try {
        ng_result = ngspice_->run_ac(cir_path);
    } catch (const std::exception& e) {
        FAIL() << "required ngspice not available or failed: " << e.what();
    }

    if (ng_result.frequency.empty()) {
        FAIL() << "required ngspice returned empty AC result (MES may not be compiled in)";
    }

    auto ckt = sim_.load(cir_path);
    ACResult cs_result = sim_.run_ac(ckt, AnalysisCommand::DEC, 10, 1e3, 10e9);

    ASSERT_FALSE(cs_result.frequency.empty());

    auto cmp = compare_ac(ng_result, cs_result, {5e-15, 1e-6});
    EXPECT_TRUE(cmp.passed)
        << "AC comparison failed. Worst: " << cmp.worst_signal
        << " error: " << cmp.worst_error;
}

TEST_F(MesValidation, NoiseSpectraAcrossPolarityGeometryAndTemperature) {
    // ngspice 47 uses independent AREA and M inputs. Keep both assignment
    // orders, AF != 1, temperature and prior device states.
    for (const std::string kind : {"thermal", "flicker", "area", "multiplier",
                                   "hot", "scaled_hot", "area_last", "pmf", "state_offset"}) {
        SCOPED_TRACE(kind);
        const std::string path = std::string(TEST_CIRCUITS_DIR) + "/mes_noise_" + kind + ".cir";
        const auto reference = ngspice_->run_noise(path);
        std::map<std::string, double> reference_geometry;
        {
            NgspiceLib reference_query;
            for (const std::string key : {"area", "m"}) {
                const auto value = reference_query.get_vec_info("@z1[" + key + "]");
                ASSERT_NE(value, nullptr);
                ASSERT_EQ(value->v_length, 1);
                ASSERT_NE(value->v_realdata, nullptr);
                ASSERT_TRUE(std::isfinite(value->v_realdata[0]));
                reference_geometry[key] = value->v_realdata[0];
            }
        }
        auto circuit = sim_.load(path);
        const auto actual = std::get<NoiseResult>(sim_.run(circuit).analysis);
        ASSERT_TRUE(actual.status.converged);
        bool found_device = false;
        for (const auto& device : circuit.devices()) {
            if (device->name() != "Z1" && device->name() != "z1") continue;
            found_device = true;
            for (const auto& [key, expected] : reference_geometry) {
                const auto value = device->query_param(key);
                ASSERT_TRUE(value.has_value()) << key;
                EXPECT_DOUBLE_EQ(*value, expected) << key;
            }
        }
        ASSERT_TRUE(found_device);
        ASSERT_EQ(actual.frequency.size(), 28u);
        const auto comparison = compare_noise(reference, actual, {1e-8, 1e-20});
        EXPECT_TRUE(comparison.passed) << comparison.worst_signal << " " << comparison.worst_error;
        ASSERT_TRUE(actual.device_noise.contains("z1"));
        const auto& device_noise = actual.device_noise.at("z1");
        ASSERT_EQ(device_noise.size(), actual.frequency.size());
        for (const double density : device_noise) {
            EXPECT_TRUE(std::isfinite(density));
            EXPECT_GT(density, 0.0);
        }
    }
}

TEST(MesMultiplicity, MatchesExplicitParallelDevicesAcrossAnalyses) {
    Simulator sim;
    auto circuit = sim.load(std::string(TEST_CIRCUITS_DIR) + "/mes_parallel.cir");
    const auto dc = sim.run_dc(circuit);
    ASSERT_TRUE(dc.status.converged);
    EXPECT_NEAR(dc.current("vddm"), dc.current("vddp"), 1e-12);
    const auto ac = sim.run_ac(circuit, AnalysisCommand::DEC, 3, 1.0, 1e9);
    ASSERT_TRUE(ac.status.converged);
    ASSERT_FALSE(ac.frequency.empty());
    for (size_t i = 0; i < ac.frequency.size(); ++i)
        EXPECT_NEAR(std::abs(ac.current("vddm")[i] - ac.current("vddp")[i]), 0.0, 1e-12);
    const auto transient = sim.run_transient(circuit, 10e-9, 2e-6);
    ASSERT_TRUE(transient.status.converged);
    ASSERT_FALSE(transient.time.empty());
    for (size_t i = 0; i < transient.time.size(); ++i)
        EXPECT_NEAR(transient.current("vddm")[i], transient.current("vddp")[i], 1e-10);
}

TEST(MesMultiplicity, NoiseSourcesAddForParallelDevices) {
    Simulator sim;
    auto circuit = sim.load(std::string(TEST_CIRCUITS_DIR) + "/mes_parallel.cir");
    ASSERT_TRUE(sim.run_dc(circuit).status.converged);
    ASSERT_NE(circuit.operating_point(), nullptr);
    std::vector<double> multiplied, parallel(4, 0.0);
    int parallel_devices = 0;
    for (const auto& device : circuit.devices()) {
        const auto& name = device->name();
        if (name == "ZM" || name == "zm") {
            const auto sources = device->noise_sources(10.0, *circuit.operating_point());
            for (const auto& source : sources) multiplied.push_back(source.spectral_density);
        } else if (name.starts_with("ZP") || name.starts_with("zp")) {
            const auto sources = device->noise_sources(10.0, *circuit.operating_point());
            ASSERT_EQ(sources.size(), 4u);
            for (size_t i = 0; i < sources.size(); ++i)
                parallel[i] += sources[i].spectral_density;
            ++parallel_devices;
        }
    }
    ASSERT_EQ(parallel_devices, 3);
    ASSERT_EQ(multiplied.size(), 4u);
    for (size_t i = 0; i < parallel.size(); ++i) {
        ASSERT_GT(parallel[i], 0.0);
        EXPECT_NEAR(multiplied[i] / parallel[i], 1.0, 1e-12);
    }
}

TEST(MesMultiplicity, RegistryBuilderPreservesGeometry) {
    ModelCard model;
    model.name = "mf";
    model.type = "nmf";
    auto& registry = DeviceRegistry::get_default();
    auto holder = registry.create_model_card("nmf", 0, model);
    ASSERT_NE(holder, nullptr);
    const std::array<int32_t, 3> nodes{0, 1, GROUND_INTERNAL};
    auto device = registry.build_device('z', "ZM", nodes,
        {{"area", 2.5}, {"m", 3.0}}, *holder);
    ASSERT_NE(device, nullptr);
    ASSERT_TRUE(device->query_param("area").has_value());
    EXPECT_DOUBLE_EQ(*device->query_param("area"), 7.5);
}
