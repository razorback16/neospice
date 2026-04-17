// Unit tests for compare_transient_oscillator: synthesized signals only
// (no simulator invocation) to verify classification, period, amplitude,
// and DC-tied behaviour independently of real circuit dynamics.

#include <gtest/gtest.h>
#include "framework/comparator.hpp"
#include <cmath>

using namespace neospice;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Synthesize a sine wave v(t) = offset + amp * sin(2*pi*f*t).
TransientResult make_sine(const std::string& node,
                          double freq_hz,
                          double amp_volts,
                          double offset_volts,
                          double duration_s,
                          size_t n_samples) {
    TransientResult tr;
    tr.time.reserve(n_samples);
    std::vector<double> y;
    y.reserve(n_samples);
    for (size_t i = 0; i < n_samples; ++i) {
        double t = duration_s * static_cast<double>(i) /
                   static_cast<double>(n_samples - 1);
        double v = offset_volts + amp_volts * std::sin(2.0 * kPi * freq_hz * t);
        tr.time.push_back(t);
        y.push_back(v);
    }
    tr.voltages[node] = std::move(y);
    return tr;
}

TransientResult make_dc(const std::string& node,
                        double level_volts,
                        double duration_s,
                        size_t n_samples) {
    TransientResult tr;
    tr.time.reserve(n_samples);
    std::vector<double> y(n_samples, level_volts);
    for (size_t i = 0; i < n_samples; ++i) {
        tr.time.push_back(duration_s * static_cast<double>(i) /
                          static_cast<double>(n_samples - 1));
    }
    tr.voltages[node] = std::move(y);
    return tr;
}

} // namespace

TEST(OscillatorComparator, IdenticalSinesPass) {
    // Two identical 1 MHz sines, 1 V amplitude, over 10 us (10 periods).
    auto a = make_sine("v(out)", 1.0e6, 1.0, 0.0, 10.0e-6, 2001);
    auto b = a;
    auto cmp = compare_transient_oscillator(a, b);
    EXPECT_TRUE(cmp.passed) << "Worst: " << cmp.worst_signal
                            << " error: " << cmp.worst_error;
}

TEST(OscillatorComparator, PeriodMismatchFails) {
    // 2% period mismatch: 1 MHz vs 1.02 MHz.
    auto a = make_sine("v(out)", 1.00e6, 1.0, 0.0, 10.0e-6, 2001);
    auto b = make_sine("v(out)", 1.02e6, 1.0, 0.0, 10.0e-6, 2001);
    OscillatorTolerance tol{}; // default period_relative = 1e-2
    auto cmp = compare_transient_oscillator(a, b, tol);
    EXPECT_FALSE(cmp.passed);
    EXPECT_NE(cmp.worst_signal.find("period"), std::string::npos)
        << "worst_signal was: " << cmp.worst_signal;
}

TEST(OscillatorComparator, AmplitudeMismatchFails) {
    // 20% amplitude mismatch, same period.
    auto a = make_sine("v(out)", 1.0e6, 1.0, 0.0, 10.0e-6, 2001);
    auto b = make_sine("v(out)", 1.0e6, 1.2, 0.0, 10.0e-6, 2001);
    OscillatorTolerance tol{}; // default amplitude_relative = 5e-2
    auto cmp = compare_transient_oscillator(a, b, tol);
    EXPECT_FALSE(cmp.passed);
    EXPECT_NE(cmp.worst_signal.find("amplitude"), std::string::npos)
        << "worst_signal was: " << cmp.worst_signal;
}

TEST(OscillatorComparator, DCSignalClassifiedAndCompared) {
    // Two flat DC signals at 1.8 V agree within dc_absolute tolerance.
    auto a = make_dc("v(vdd)", 1.800, 10.0e-6, 501);
    auto b = make_dc("v(vdd)", 1.810, 10.0e-6, 501); // 10 mV off
    OscillatorTolerance tol{}; // dc_absolute = 50 mV
    auto cmp = compare_transient_oscillator(a, b, tol);
    EXPECT_TRUE(cmp.passed) << "Worst: " << cmp.worst_signal
                            << " error: " << cmp.worst_error;

    // Now exceed dc_absolute: 100 mV off.
    auto c = make_dc("v(vdd)", 1.900, 10.0e-6, 501);
    auto cmp2 = compare_transient_oscillator(a, c, tol);
    EXPECT_FALSE(cmp2.passed);
    EXPECT_NE(cmp2.worst_signal.find("dc"), std::string::npos)
        << "worst_signal was: " << cmp2.worst_signal;
}

TEST(OscillatorComparator, ClassificationMismatchFails) {
    // Expected DC, actual oscillates -> clear failure message.
    auto a = make_dc("v(out)", 0.9, 10.0e-6, 501);
    auto b = make_sine("v(out)", 1.0e6, 0.8, 0.9, 10.0e-6, 2001);
    auto cmp = compare_transient_oscillator(a, b);
    EXPECT_FALSE(cmp.passed);
    EXPECT_NE(cmp.worst_signal.find("DC"), std::string::npos)
        << "worst_signal was: " << cmp.worst_signal;
}

TEST(OscillatorComparator, DetectsMidOffsetShift) {
    // Two 1 MHz sines with same period (1 MHz) and amplitude (1 V), but one
    // is DC-shifted by 0.5 V: expected mid = 0 V, actual mid = 0.5 V.
    // With mid_absolute = 0.1 (100 mV), the 0.5 V shift should fail and
    // report worst_signal containing "mid-offset".
    auto expected = make_sine("v(out)", 1.0e6, 1.0, 0.0, 10.0e-6, 2001);
    auto actual   = make_sine("v(out)", 1.0e6, 1.0, 0.5, 10.0e-6, 2001);
    OscillatorTolerance tol{};
    tol.mid_absolute = 0.1;  // 100 mV tolerance — 500 mV shift should fail
    auto cmp = compare_transient_oscillator(expected, actual, tol);
    EXPECT_FALSE(cmp.passed);
    EXPECT_NE(cmp.worst_signal.find("mid-offset"), std::string::npos)
        << "worst_signal was: " << cmp.worst_signal;
}
