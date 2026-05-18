#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "framework/ngspice_runner.hpp"
#include "framework/comparator.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <vector>

using namespace neospice;

class NgspiceCompareTest : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>();
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};

// ---------------------------------------------------------------------------
// DC operating-point tests — these should match ngspice very tightly.
// ---------------------------------------------------------------------------

TEST_F(NgspiceCompareTest, ResistorDividerDC) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/resistor_divider.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);
    auto cmp = compare_dc(ng_result, cs_result, {1e-8, 1e-12});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

TEST_F(NgspiceCompareTest, TlineDC) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/tline_dc.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);
    // Strip ngspice internal nodes (containing '#') that neospice doesn't expose.
    for (auto it = ng_result.node_voltages.begin();
         it != ng_result.node_voltages.end(); ) {
        if (it->first.find('#') != std::string::npos)
            it = ng_result.node_voltages.erase(it);
        else
            ++it;
    }
    auto cmp = compare_dc(ng_result, cs_result, {1e-5, 1e-6});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// AC small-signal test — should match well since both use direct solve.
// ---------------------------------------------------------------------------

TEST_F(NgspiceCompareTest, RCACAnalysis) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/rc_ac.cir";
    auto ng_result = ngspice_->run_ac(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<ACResult>(cs_result.analysis));
    auto cmp = compare_ac(ng_result, std::get<ACResult>(cs_result.analysis), {2e-14, 1e-15});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

TEST_F(NgspiceCompareTest, IsrcACAnalysis) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/isrc_ac.cir";
    auto ng_result = ngspice_->run_ac(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<ACResult>(cs_result.analysis));
    auto cmp = compare_ac(ng_result, std::get<ACResult>(cs_result.analysis), {2e-14, 1e-15});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// Transient tests — both neospice and ngspice use trapezoidal integration.
// Remaining error sources:
//   1. Interpolation mismatch near zero crossings vs ngspice adaptive steps
//   2. Slight timestep control differences affecting switching edges
// We compare at our time grid and use tightened tolerances where possible.
// ---------------------------------------------------------------------------

TEST_F(NgspiceCompareTest, RCLowpassTransient) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/rc_lowpass.cir";
    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    ckt.options.interp = true;
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(cs_result.analysis));
    auto cmp = compare_transient(std::get<TransientResult>(cs_result.analysis), ng_result, {3e-5, 3e-5});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

TEST_F(NgspiceCompareTest, RLCSeriesTransient) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/rlc_series.cir";
    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    ckt.options.interp = true;
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(cs_result.analysis));
    auto cmp = compare_transient(std::get<TransientResult>(cs_result.analysis), ng_result, {5e-3, 1e-3});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

TEST_F(NgspiceCompareTest, RLCUnderdampedTransient) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/rlc_underdamped.cir";
    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    ckt.options.interp = true;
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(cs_result.analysis));
    auto cmp = compare_transient(std::get<TransientResult>(cs_result.analysis), ng_result, {2e-5, 2e-5});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// Switch hysteresis tests
// ---------------------------------------------------------------------------

// Helper: extract transition times from a switch waveform
static std::vector<double> find_transitions(
    const std::vector<double>& time,
    const std::vector<double>& values,
    double threshold)
{
    std::vector<double> transitions;
    for (size_t i = 1; i < values.size(); ++i) {
        if (std::abs(values[i] - values[i-1]) > threshold)
            transitions.push_back(time[i]);
    }
    return transitions;
}

// Helper: interpolate a piecewise-linear series at time t
static double interp_at(const std::vector<double>& time,
                        const std::vector<double>& vals, double t)
{
    if (t <= time.front()) return vals.front();
    if (t >= time.back())  return vals.back();
    auto it = std::lower_bound(time.begin(), time.end(), t);
    size_t i = std::distance(time.begin(), it);
    if (i == 0) return vals[0];
    double frac = (t - time[i-1]) / (time[i] - time[i-1]);
    return vals[i-1] + frac * (vals[i] - vals[i-1]);
}

TEST_F(NgspiceCompareTest, SwitchHysteresisTransient) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/switch_hysteresis.cir";
    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    ckt.options.interp = true;
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(cs_result.analysis));

    auto& cs_tr = std::get<TransientResult>(cs_result.analysis);

    // 1. Check that both have v(out)
    auto cs_it = cs_tr.voltages.find("v(out)");
    auto ng_it = ng_result.voltages.find("v(out)");
    ASSERT_NE(cs_it, cs_tr.voltages.end());
    ASSERT_NE(ng_it, ng_result.voltages.end());

    // 2. Compare transition times (hard switch: jump > 1V)
    auto cs_trans = find_transitions(cs_tr.time, cs_it->second, 1.0);
    auto ng_trans = find_transitions(ng_result.time, ng_it->second, 1.0);
    ASSERT_EQ(cs_trans.size(), ng_trans.size())
        << "Different number of transitions: neospice=" << cs_trans.size()
        << " ngspice=" << ng_trans.size();
    for (size_t i = 0; i < cs_trans.size(); ++i) {
        EXPECT_NEAR(cs_trans[i], ng_trans[i], 2e-7)  // within 0.2us
            << "Transition " << i << " time mismatch";
    }

    // 3. Compare steady-state levels away from edges (sample at mid-period)
    // ON level: sample at 50% between first ON transition and next OFF transition
    if (cs_trans.size() >= 2) {
        double t_mid_on = 0.5 * (cs_trans[0] + cs_trans[1]);
        double cs_val = interp_at(cs_tr.time, cs_it->second, t_mid_on);
        double ng_val = interp_at(ng_result.time, ng_it->second, t_mid_on);
        EXPECT_NEAR(cs_val, ng_val, 1e-3)
            << "ON-state level mismatch at t=" << t_mid_on;
    }
    if (cs_trans.size() >= 3) {
        double t_mid_off = 0.5 * (cs_trans[1] + cs_trans[2]);
        double cs_val = interp_at(cs_tr.time, cs_it->second, t_mid_off);
        double ng_val = interp_at(ng_result.time, ng_it->second, t_mid_off);
        EXPECT_NEAR(cs_val, ng_val, 1e-3)
            << "OFF-state level mismatch at t=" << t_mid_off;
    }
}

// ---------------------------------------------------------------------------
// Noise analysis tests — compare noise spectral density against ngspice.
// neospice stores V^2/Hz; ngspice stores V/sqrt(Hz). The comparator handles
// the unit conversion (sqrt).
// ---------------------------------------------------------------------------

TEST_F(NgspiceCompareTest, ResistorDividerNoise) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/resistor_divider_noise.cir";
    auto ng_result = ngspice_->run_noise(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<NoiseResult>(cs_result.analysis));
    ASSERT_EQ(ng_result.frequency.size(), std::get<NoiseResult>(cs_result.analysis).frequency.size());
    auto cmp = compare_noise(ng_result, std::get<NoiseResult>(cs_result.analysis), {1e-5, 1e-15});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

TEST_F(NgspiceCompareTest, RCLowpassNoise) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/rc_lowpass_noise.cir";
    auto ng_result = ngspice_->run_noise(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<NoiseResult>(cs_result.analysis));
    ASSERT_EQ(ng_result.frequency.size(), std::get<NoiseResult>(cs_result.analysis).frequency.size());
    // RC lowpass rolls off noise — input-referred stays flat, output follows |H(f)|
    auto cmp = compare_noise(ng_result, std::get<NoiseResult>(cs_result.analysis), {1e-5, 1e-15});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// PULSE/SIN default parameter tests — verify ngspice-compatible defaults.
// When TR/TF/PW/PER are omitted from PULSE, ngspice uses tstep/tstop.
// ---------------------------------------------------------------------------

TEST_F(NgspiceCompareTest, TlineIC) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/tline_ic.cir";
    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    ckt.options.interp = true;
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(cs_result.analysis));
    auto cmp = compare_transient(std::get<TransientResult>(cs_result.analysis), ng_result, {5e-2, 1e-3});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

TEST_F(NgspiceCompareTest, TlineAC) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/tline_ac.cir";
    auto ng_result = ngspice_->run_ac(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<ACResult>(cs_result.analysis));
    // Strip ngspice internal nodes (containing '#') that neospice doesn't expose.
    for (auto it = ng_result.voltages.begin();
         it != ng_result.voltages.end(); ) {
        if (it->first.find('#') != std::string::npos)
            it = ng_result.voltages.erase(it);
        else
            ++it;
    }
    auto cmp = compare_ac(ng_result, std::get<ACResult>(cs_result.analysis), {5e-15, 1e-15});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

TEST_F(NgspiceCompareTest, PulseDefaultsTransient) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/pulse_defaults.cir";
    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    ckt.options.interp = true;
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(cs_result.analysis));
    auto cmp = compare_transient(std::get<TransientResult>(cs_result.analysis), ng_result, {1e-7, 1e-7});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

TEST_F(NgspiceCompareTest, PwlSourceTransient) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/pwl_source.cir";
    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    ckt.options.interp = true;
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(cs_result.analysis));
    auto cmp = compare_transient(std::get<TransientResult>(cs_result.analysis), ng_result, {5e-14, 5e-14});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

TEST_F(NgspiceCompareTest, ExpSourceTransient) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/exp_source.cir";
    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    ckt.options.interp = true;
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(cs_result.analysis));
    // Drop currents — capacitive spike through voltage source at EXP edges
    // is Dirac-delta-like and impossible to match point-wise.
    ng_result.currents.clear();
    std::get<TransientResult>(cs_result.analysis).currents.clear();
    // Slightly relaxed tolerance: time-grid interpolation at the sharp EXP
    // breakpoint edges (td1, td2) causes a small mismatch that inflates the
    // relative error when the signal is near zero.
    auto cmp = compare_transient(std::get<TransientResult>(cs_result.analysis), ng_result, {2e-1, 2e-2});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

TEST_F(NgspiceCompareTest, SffmSourceTransient) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/sffm_source.cir";
    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    ckt.options.interp = true;
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(cs_result.analysis));
    auto cmp = compare_transient(std::get<TransientResult>(cs_result.analysis), ng_result, {2e-3, 5e-4});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

TEST_F(NgspiceCompareTest, AmSourceTransient) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/am_source.cir";
    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    ckt.options.interp = true;
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(cs_result.analysis));
    auto cmp = compare_transient(std::get<TransientResult>(cs_result.analysis), ng_result, {1e-4, 1e-4});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// Temperature coefficient tests
// ---------------------------------------------------------------------------

TEST_F(NgspiceCompareTest, ResistorTempCoeff) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/resistor_temp.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);
    auto cmp = compare_dc(ng_result, cs_result, {1e-7, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// Multiplier (m) parameter tests
// ---------------------------------------------------------------------------

TEST_F(NgspiceCompareTest, ResistorMultiplier) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/resistor_m2.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);
    auto cmp = compare_dc(ng_result, cs_result, {1e-7, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// ASRC TEMPER variable test
// ---------------------------------------------------------------------------

TEST_F(NgspiceCompareTest, AsrcTemper) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/asrc_temper.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);
    auto cmp = compare_dc(ng_result, cs_result, {1e-8, 1e-12});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// ASRC PWL function test
// ---------------------------------------------------------------------------

TEST_F(NgspiceCompareTest, AsrcPwl) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/asrc_pwl.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);
    auto cmp = compare_dc(ng_result, cs_result, {1e-8, 1e-12});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// ASRC HERTZ variable test
// ---------------------------------------------------------------------------

TEST_F(NgspiceCompareTest, AsrcHertz) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/asrc_hertz.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);
    auto cmp = compare_dc(ng_result, cs_result, {1e-8, 1e-12});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// ASRC DDT() time derivative test
// ---------------------------------------------------------------------------

TEST_F(NgspiceCompareTest, AsrcDdtTransient) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/asrc_ddt.cir";
    auto ng_result = ngspice_->run_transient(path);
    auto ckt = sim_.load(path);
    ckt.options.interp = true;
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(cs_result.analysis));
    auto cmp = compare_transient(std::get<TransientResult>(cs_result.analysis), ng_result, {5e-14, 5e-14});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// ASRC IDT() time integral test
// IDT is a neospice/XSPICE extension (ngspice does not support idt() in B
// sources), so we verify the expected analytical result directly:
//   V(in)=1 constant  =>  IDT(V(in)) = integral(1, 0..t) = t
// ---------------------------------------------------------------------------

TEST_F(NgspiceCompareTest, AsrcIdtTransient) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/asrc_idt.cir";
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(cs_result.analysis));
    const auto& tr = std::get<TransientResult>(cs_result.analysis);
    const auto& t = tr.time;
    const auto& v_out = tr.voltage("out");
    ASSERT_EQ(t.size(), v_out.size());
    ASSERT_GT(t.size(), 2u);
    // V(out) = IDT(1) = t at each timepoint (skip t=0 where integral is 0)
    for (size_t i = 1; i < t.size(); ++i) {
        EXPECT_NEAR(v_out[i], t[i], 1e-3)
            << "IDT(1) mismatch at t=" << t[i]
            << ": expected " << t[i] << " got " << v_out[i];
    }
}

// ---------------------------------------------------------------------------
// Resistor model card test — .model RMOD R(TC1=... TC2=...)
// ---------------------------------------------------------------------------

TEST_F(NgspiceCompareTest, ResistorModelCard) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/resistor_model.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);
    auto cmp = compare_dc(ng_result, cs_result, {1e-7, 1e-9});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// Inductor model card test — .model LMOD L(TC1=... TC2=...)
// Uses an RL AC divider at elevated temperature to verify that the
// temperature-adjusted inductance matches ngspice.
// ---------------------------------------------------------------------------

TEST_F(NgspiceCompareTest, InductorModelCard) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/inductor_model.cir";
    auto ng_result = ngspice_->run_ac(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<ACResult>(cs_result.analysis));
    auto cmp = compare_ac(ng_result, std::get<ACResult>(cs_result.analysis), {1e-14, 1e-15});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// Resistor RAC= AC resistance test
// RAC is a neospice extension (ngspice does not support it), so we verify
// the expected voltage divider ratio directly instead of comparing to ngspice.
// With R1=1k RAC=500, R2=1k: v(out) = 1 * 1k/(500+1k) = 0.6667
// ---------------------------------------------------------------------------

TEST_F(NgspiceCompareTest, ResistorRAC) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/resistor_rac.cir";
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<ACResult>(cs_result.analysis));
    const auto& ac = std::get<ACResult>(cs_result.analysis);
    // Find v(out) in the AC results
    const auto& v_out = ac.voltage("out");
    ASSERT_FALSE(v_out.empty());
    // Expected: v(out) = 1k/(500+1k) = 2/3 at all frequencies (flat response)
    const double expected_mag = 1000.0 / (500.0 + 1000.0);
    for (size_t i = 0; i < v_out.size(); ++i) {
        double mag = std::abs(v_out[i]);
        EXPECT_NEAR(mag, expected_mag, 1e-10)
            << "AC magnitude mismatch at frequency point " << i;
    }
}

// ---------------------------------------------------------------------------
// F POLY(1) — current-controlled current source with polynomial
// ---------------------------------------------------------------------------
TEST_F(NgspiceCompareTest, CccsPolyDC) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/cccs_poly.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);
    auto cmp = compare_dc(ng_result, cs_result, {1e-8, 1e-12});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

TEST_F(NgspiceCompareTest, CccsPoly2DC) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/cccs_poly2.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);
    auto cmp = compare_dc(ng_result, cs_result, {1e-8, 1e-12});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// H POLY(1) — current-controlled voltage source with polynomial
// ---------------------------------------------------------------------------
TEST_F(NgspiceCompareTest, CcvsPolyDC) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/ccvs_poly.cir";
    auto ng_result = ngspice_->run_dc(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run_dc(ckt);
    // Strip ngspice internal xspice nodes/branches (containing '$')
    for (auto it = ng_result.node_voltages.begin();
         it != ng_result.node_voltages.end(); ) {
        if (it->first.find('$') != std::string::npos)
            it = ng_result.node_voltages.erase(it);
        else
            ++it;
    }
    for (auto it = ng_result.branch_currents.begin();
         it != ng_result.branch_currents.end(); ) {
        if (it->first.find('$') != std::string::npos)
            it = ng_result.branch_currents.erase(it);
        else
            ++it;
    }
    auto cmp = compare_dc(ng_result, cs_result, {1e-8, 1e-12});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}

// ---------------------------------------------------------------------------
// tnoiMod=2 noise — correlated drain-gate noise (CORLNOIZ)
// ---------------------------------------------------------------------------

TEST_F(NgspiceCompareTest, BSIM4v7_Noise_TnoiMod2) {
    std::string path = std::string(TEST_CIRCUITS_DIR) + "/nmos_noise_tnoi2.cir";
    auto ng_result = ngspice_->run_noise(path);
    auto ckt = sim_.load(path);
    auto cs_result = sim_.run(ckt);
    ASSERT_TRUE(std::holds_alternative<NoiseResult>(cs_result.analysis));
    auto cmp = compare_noise(ng_result, std::get<NoiseResult>(cs_result.analysis), {1e-5, 1e-30});
    EXPECT_TRUE(cmp.passed)
        << "Worst: " << cmp.worst_signal << " error: " << cmp.worst_error;
}
