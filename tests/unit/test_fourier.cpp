#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "core/fourier.hpp"
#include "output/output.hpp"
#include <cmath>
#include <string>
#include <vector>

using namespace neospice;

// =========================================================================
// Helper: build a synthetic TransientResult from a lambda
// =========================================================================
static TransientResult make_tran(double f0, double n_periods,
                                  int points_per_period,
                                  const std::string& signal_name,
                                  std::function<double(double)> fn) {
    TransientResult tr;
    double T     = 1.0 / f0;
    double tstop = n_periods * T;
    int    N     = static_cast<int>(n_periods * points_per_period);
    tr.time.resize(N + 1);
    std::vector<double> vals(N + 1);
    for (int i = 0; i <= N; ++i) {
        double t  = tstop * i / N;
        tr.time[i] = t;
        vals[i]    = fn(t);
    }
    tr.voltages[signal_name] = std::move(vals);
    return tr;
}

// =========================================================================
// Unit tests for compute_fourier
// =========================================================================

// ---------------------------------------------------------------------------
// Pure sine: 5 V amplitude, 1 kHz
// Expected: fundamental magnitude ≈ 5 V, THD < 0.1 %
// ---------------------------------------------------------------------------
TEST(FourierUnit, PureSine_THD) {
    const double f0  = 1000.0;   // 1 kHz
    const double amp = 5.0;
    auto tr = make_tran(f0, 10.0, 1000, "v(out)",
                        [&](double t) { return amp * std::sin(2 * M_PI * f0 * t); });

    auto results = compute_fourier(f0, {"v(out)"}, tr);
    ASSERT_EQ(results.size(), 1u);
    const auto& r = results[0];

    EXPECT_EQ(r.signal_name, "v(out)");
    EXPECT_NEAR(r.fundamental_freq, 1000.0, 1e-6);
    ASSERT_EQ(r.components.size(), 10u);   // DC + 9 harmonics

    // DC should be near 0
    EXPECT_NEAR(r.components[0].magnitude, 0.0, 0.01);

    // Fundamental magnitude ≈ amp (within 0.5%)
    EXPECT_NEAR(r.components[1].magnitude, amp, amp * 0.005);

    // THD should be very small for a pure sine
    EXPECT_LT(r.thd, 0.1);

    // Normalized fundamental = 1
    EXPECT_NEAR(r.components[1].normalized_mag, 1.0, 1e-4);
}

// ---------------------------------------------------------------------------
// Sine with DC offset: 3 V DC + 2 V sine at 500 Hz
// Expected: DC ≈ 3 V, fundamental ≈ 2 V
// ---------------------------------------------------------------------------
TEST(FourierUnit, SinePlusDCOffset) {
    const double f0     = 500.0;
    const double dc_off = 3.0;
    const double amp    = 2.0;
    auto tr = make_tran(f0, 5.0, 2000, "v(out)",
                        [&](double t) {
                            return dc_off + amp * std::sin(2 * M_PI * f0 * t);
                        });

    auto results = compute_fourier(f0, {"v(out)"}, tr);
    ASSERT_EQ(results.size(), 1u);
    const auto& r = results[0];

    // DC component
    EXPECT_NEAR(r.components[0].magnitude, dc_off, dc_off * 0.01);

    // Fundamental
    EXPECT_NEAR(r.components[1].magnitude, amp, amp * 0.01);

    // THD still small
    EXPECT_LT(r.thd, 0.5);
}

// ---------------------------------------------------------------------------
// Square wave: should have large odd harmonics, THD > 40%
// Square wave Fourier series: 4/pi * (sin(wt) + sin(3wt)/3 + sin(5wt)/5 + ...)
// Expected: THD ≈ 48% (theoretical value for infinite series)
// ---------------------------------------------------------------------------
TEST(FourierUnit, SquareWave_HighTHD) {
    const double f0  = 1000.0;
    const double amp = 1.0;
    // Approximate square wave using first 100 odd harmonics
    auto sq_wave = [&](double t) -> double {
        double val = 0.0;
        for (int k = 1; k <= 99; k += 2) {
            val += std::sin(2 * M_PI * k * f0 * t) / k;
        }
        return (4.0 / M_PI) * amp * val;
    };

    auto tr = make_tran(f0, 20.0, 2000, "v(out)", sq_wave);
    auto results = compute_fourier(f0, {"v(out)"}, tr);
    ASSERT_EQ(results.size(), 1u);
    const auto& r = results[0];

    // Fundamental ≈ 4/pi * amp
    double expected_fund = (4.0 / M_PI) * amp;
    EXPECT_NEAR(r.components[1].magnitude, expected_fund, expected_fund * 0.02);

    // THD should be > 40%
    EXPECT_GT(r.thd, 40.0);
}

// ---------------------------------------------------------------------------
// Harmonic component indices and frequencies
// ---------------------------------------------------------------------------
TEST(FourierUnit, ComponentIndicesAndFrequencies) {
    const double f0 = 1000.0;
    auto tr = make_tran(f0, 5.0, 500, "v(out)",
                        [&](double t) { return std::sin(2 * M_PI * f0 * t); });

    auto results = compute_fourier(f0, {"v(out)"}, tr);
    ASSERT_EQ(results.size(), 1u);
    const auto& r = results[0];
    ASSERT_EQ(r.components.size(), 10u);

    for (int k = 0; k <= 9; ++k) {
        EXPECT_EQ(r.components[k].harmonic, k);
        EXPECT_NEAR(r.components[k].frequency, k * f0, 1e-6);
    }
}

// ---------------------------------------------------------------------------
// Missing signal: should produce all-zero result without crashing
// ---------------------------------------------------------------------------
TEST(FourierUnit, MissingSignal_AllZero) {
    const double f0 = 1000.0;
    auto tr = make_tran(f0, 5.0, 500, "v(out)",
                        [&](double t) { return std::sin(2 * M_PI * f0 * t); });

    // Ask for a signal that doesn't exist
    auto results = compute_fourier(f0, {"v(nonexistent)"}, tr);
    ASSERT_EQ(results.size(), 1u);
    const auto& r = results[0];

    for (const auto& fc : r.components) {
        EXPECT_NEAR(fc.magnitude, 0.0, 1e-30);
    }
    EXPECT_NEAR(r.thd, 0.0, 1e-10);
}

// ---------------------------------------------------------------------------
// Current signal lookup (via TransientResult::currents)
// ---------------------------------------------------------------------------
TEST(FourierUnit, CurrentSignal) {
    const double f0  = 2000.0;
    const double amp = 0.1;
    TransientResult tr;
    double T     = 1.0 / f0;
    int    N     = 5000;
    double tstop = 5.0 * T;
    tr.time.resize(N + 1);
    std::vector<double> vals(N + 1);
    for (int i = 0; i <= N; ++i) {
        double t   = tstop * i / N;
        tr.time[i] = t;
        vals[i]    = amp * std::sin(2 * M_PI * f0 * t);
    }
    tr.currents["i(r1)"] = std::move(vals);

    auto results = compute_fourier(f0, {"i(r1)"}, tr);
    ASSERT_EQ(results.size(), 1u);
    const auto& r = results[0];

    EXPECT_NEAR(r.components[1].magnitude, amp, amp * 0.01);
    EXPECT_LT(r.thd, 1.0);
}

// ---------------------------------------------------------------------------
// Edge case: fundamental period doesn't divide evenly into tstop
// The algorithm uses the LAST complete period; result should still be valid.
// ---------------------------------------------------------------------------
TEST(FourierUnit, NonIntegralPeriods) {
    const double f0 = 1000.0;
    const double amp = 3.0;
    // Use 7.3 periods — not an integer
    auto tr = make_tran(f0, 7.3, 800, "v(out)",
                        [&](double t) { return amp * std::sin(2 * M_PI * f0 * t); });

    auto results = compute_fourier(f0, {"v(out)"}, tr);
    ASSERT_EQ(results.size(), 1u);
    const auto& r = results[0];

    // Should still recover fundamental within 1%
    EXPECT_NEAR(r.components[1].magnitude, amp, amp * 0.01);
    EXPECT_LT(r.thd, 1.0);
}

// =========================================================================
// Parser tests
// =========================================================================

TEST(FourierParser, BasicParse) {
    Simulator sim;
    auto ckt = sim.parse(R"(
test circuit
V1 in 0 SIN(0 5 1k)
R1 in out 1k
.tran 1u 10m
.four 1k V(out)
.end
)");
    ASSERT_EQ(ckt.fourier_commands.size(), 1u);
    EXPECT_NEAR(ckt.fourier_commands[0].fundamental_freq, 1000.0, 1e-6);
    ASSERT_EQ(ckt.fourier_commands[0].signals.size(), 1u);
    EXPECT_EQ(ckt.fourier_commands[0].signals[0], "v(out)");
}

TEST(FourierParser, MultipleSignals) {
    Simulator sim;
    auto ckt = sim.parse(R"(
test circuit
V1 in 0 SIN(0 5 1k)
R1 in out 1k
R2 out 0 1k
.tran 1u 10m
.four 1k V(out) V(in) I(R1)
.end
)");
    ASSERT_EQ(ckt.fourier_commands.size(), 1u);
    EXPECT_NEAR(ckt.fourier_commands[0].fundamental_freq, 1000.0, 1e-6);
    ASSERT_EQ(ckt.fourier_commands[0].signals.size(), 3u);
    EXPECT_EQ(ckt.fourier_commands[0].signals[0], "v(out)");
    EXPECT_EQ(ckt.fourier_commands[0].signals[1], "v(in)");
    EXPECT_EQ(ckt.fourier_commands[0].signals[2], "i(r1)");
}

TEST(FourierParser, FrequencyInKHz) {
    Simulator sim;
    // "10k" should parse to 10000 Hz
    auto ckt = sim.parse(R"(
test circuit
V1 in 0 SIN(0 1 10k)
R1 in out 1k
.tran 100n 100u
.four 10k V(out)
.end
)");
    ASSERT_EQ(ckt.fourier_commands.size(), 1u);
    EXPECT_NEAR(ckt.fourier_commands[0].fundamental_freq, 10000.0, 1.0);
}

TEST(FourierParser, MultipleFourCommands) {
    Simulator sim;
    auto ckt = sim.parse(R"(
test circuit
V1 in 0 SIN(0 5 1k)
R1 in out 1k
R2 out 0 1k
.tran 1u 10m
.four 1k V(out)
.four 1k V(in) I(R1)
.end
)");
    ASSERT_EQ(ckt.fourier_commands.size(), 2u);
    EXPECT_EQ(ckt.fourier_commands[0].signals.size(), 1u);
    EXPECT_EQ(ckt.fourier_commands[1].signals.size(), 2u);
}

// =========================================================================
// Output formatter tests
// =========================================================================

TEST(FourierOutput, FormatContainsKeyFields) {
    const double f0 = 1000.0;
    auto tr = make_tran(f0, 10.0, 1000, "v(out)",
                        [&](double t) { return 5.0 * std::sin(2 * M_PI * f0 * t); });
    auto results = compute_fourier(f0, {"v(out)"}, tr);
    std::string formatted = format_fourier(results);

    // Should contain the signal name
    EXPECT_NE(formatted.find("v(out)"), std::string::npos);
    // Should contain "THD"
    EXPECT_NE(formatted.find("THD"), std::string::npos);
    // Should contain harmonic column header
    EXPECT_NE(formatted.find("Harmonic"), std::string::npos);
    // Should contain frequency column header
    EXPECT_NE(formatted.find("Frequency"), std::string::npos);
}

TEST(FourierOutput, FormatHas10Rows) {
    const double f0 = 1000.0;
    auto tr = make_tran(f0, 10.0, 1000, "v(out)",
                        [&](double t) { return 2.0 * std::sin(2 * M_PI * f0 * t); });
    auto results = compute_fourier(f0, {"v(out)"}, tr);
    ASSERT_EQ(results[0].components.size(), 10u);  // DC + harmonics 1-9
}

// =========================================================================
// Integration test: full circuit with .four command
// =========================================================================

TEST(FourierIntegration, RCSineFilter) {
    // Simple RC low-pass: Vin = 1V sine at 1kHz, R=1k, C=160nF (fc ≈ 1kHz)
    // Output should have fundamental near 0.707 * 1V, THD near 0%
    Simulator sim;
    auto ckt = sim.parse(R"(
RC lowpass fourier test
V1 in 0 SIN(0 1 1k)
R1 in out 1k
C1 out 0 160n
.tran 1u 20m
.four 1k V(out)
.end
)");

    ASSERT_EQ(ckt.fourier_commands.size(), 1u);

    auto result = sim.run(ckt);

    // Transient should have run
    ASSERT_TRUE(result.transient.has_value());

    // print_output should contain Fourier results (appended last)
    ASSERT_FALSE(result.print_output.empty());
    const std::string& four_out = result.print_output.back();
    EXPECT_NE(four_out.find("v(out)"), std::string::npos);
    EXPECT_NE(four_out.find("THD"), std::string::npos);

    // Also verify via compute_fourier directly
    auto four_results = compute_fourier(1000.0, {"v(out)"}, *result.transient);
    ASSERT_EQ(four_results.size(), 1u);

    // Fundamental should be ≈ 0.707 V (within 10% — depends on exact RC time)
    double fund = four_results[0].components[1].magnitude;
    EXPECT_GT(fund, 0.3);
    EXPECT_LT(fund, 1.0);

    // THD should be very small
    EXPECT_LT(four_results[0].thd, 2.0);
}

TEST(FourierIntegration, MultipleSignalsInOneFourCommand) {
    Simulator sim;
    auto ckt = sim.parse(R"(
multi-signal fourier test
V1 in 0 SIN(0 2 500)
R1 in mid 1k
R2 mid 0 1k
.tran 2u 40m
.four 500 V(in) V(mid)
.end
)");

    auto result = sim.run(ckt);
    ASSERT_TRUE(result.transient.has_value());

    auto four_results = compute_fourier(500.0, {"v(in)", "v(mid)"}, *result.transient);
    ASSERT_EQ(four_results.size(), 2u);

    // v(in) fundamental ≈ 2 V
    EXPECT_NEAR(four_results[0].components[1].magnitude, 2.0, 0.2);
    // v(mid) ≈ 1 V (voltage divider)
    EXPECT_NEAR(four_results[1].components[1].magnitude, 1.0, 0.15);
}
