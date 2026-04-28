// JFET2 (Parker-Skellern) validation suite.
// Compares neospice JFET2 device output against ngspice for:
//   1. NJF DC operating point
//   2. NJF AC small-signal
//   3. NJF switching transient

#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "framework/ngspice_runner.hpp"
#include "framework/comparator.hpp"

using namespace neospice;

class JFET2Validation : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>(NGSPICE_BINARY);
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// Helper: strip ngspice internal nodes (names containing '#')
static void strip_internal_dc(DCResult& r) {
    for (auto it = r.node_voltages.begin(); it != r.node_voltages.end(); ) {
        if (it->first.find('#') != std::string::npos)
            it = r.node_voltages.erase(it);
        else
            ++it;
    }
}

template <typename Map>
static void strip_internal(Map& m) {
    for (auto it = m.begin(); it != m.end(); ) {
        if (it->first.find('#') != std::string::npos)
            it = m.erase(it);
        else
            ++it;
    }
}

// ============================================================================
// 1.  NJF DC Operating Point
// ============================================================================

TEST_F(JFET2Validation, NjfDcOperatingPoint) {
    // Common-source NJF amplifier with JFET2 LEVEL=2 model.
    // Vdd=5V, Rd=1k, Vg=-0.5V (gate biased above VTO=-2V, in active).
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/jfet2_njf_dc.cir";

    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);

    strip_internal_dc(ng_result);

    auto cmp = compare_dc(ng_result, cs_result, {1e-3, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ============================================================================
// 2.  NJF AC Small-Signal
// ============================================================================

TEST_F(JFET2Validation, NjfAcSmallSignal) {
    // Common-source amplifier AC sweep (100 Hz to 1 GHz).
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/jfet2_njf_ac.cir";

    auto ng_result = ngspice_->run_ac(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<ACResult>(cs_result.analysis));

    strip_internal(ng_result.voltages);
    strip_internal(ng_result.currents);

    auto cmp = compare_ac(ng_result, std::get<ACResult>(cs_result.analysis), {1e-6, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ============================================================================
// 3.  NJF Switching Transient
// ============================================================================

TEST_F(JFET2Validation, NjfSwitchingTransient) {
    // JFET2 NJF switching transient: pulse input drives gate,
    // drain loaded with Rd.  Compare waveforms.
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/jfet2_njf_tran.cir";

    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(cs_result.analysis));

    strip_internal(ng_result.voltages);
    strip_internal(ng_result.currents);

    auto cmp = compare_transient(std::get<TransientResult>(cs_result.analysis), ng_result, {1e-1, 1e-3});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}
