#include <gtest/gtest.h>
#include "bench/tlv3201_validation.hpp"
#include <limits>

using namespace neospice;
using namespace neospice::bench::driver;
namespace {
TransientResult waveform(double shift = 0) {
    TransientResult r;
    r.status.converged = true;
    r.time = {0, 0.9e-6+shift, 1.1e-6+shift, 3e-6, 30e-6};
    r.voltages = {{"v(out)", {0,0,5,5,5}}, {"v(vcc)", {5,5,5,5,5}},
                  {"v(inm)", {2.5,2.5,2.5,2.5,2.5}}, {"v(inp)", {2,2,3,3,3}}};
    return r;
}
}

TEST(Tlv3201Validation, RetainsOriginalCrossingAllowanceAndSeparatesWaveformDiagnostic) {
    Tlv3201Validation check;
    auto ref=waveform();
    auto c=check.compare(ref, waveform(49e-9));
    ASSERT_TRUE(c.passed);
    EXPECT_NEAR(c.worst_error, 0.98, 1e-12);
    ASSERT_EQ(check.expected_edges.size(), 1);
    ASSERT_EQ(c.signals.size(), 3);
    EXPECT_NEAR(c.signals.back().max_absolute_error, 49e-9, 1e-20);
    EXPECT_FALSE(check.waveform.passed); // pointwise differences remain visible
    c=check.compare(ref, waveform(51e-9));
    EXPECT_FALSE(c.passed);
    EXPECT_NEAR(c.worst_error, 1.02, 1e-12);
}

TEST(Tlv3201Validation, RejectsEmptyMissingUnequalAndOppositeEdges) {
    auto ref=waveform(); auto actual=ref; Tlv3201Validation check;
    ref.voltages["v(out)"].assign(5, 0); actual=ref;
    EXPECT_FALSE(check.compare(ref, actual).passed);
    ref=waveform(); EXPECT_FALSE(check.compare(ref, actual).passed);
    actual=ref; actual.voltages["v(out)"]={3.3,3.3,-1.7,-1.7,-1.7};
    EXPECT_FALSE(check.compare(ref, actual).passed);
    actual=ref; actual.voltages.erase("v(inp)");
    EXPECT_FALSE(check.compare(ref, actual).passed);
}

TEST(Tlv3201Validation, RejectsUnsuccessfulNonfiniteMalformedAndTruncatedData) {
    const auto ref=waveform(); Tlv3201Validation check;
    auto n=ref; n.status.converged=false; EXPECT_FALSE(check.compare(ref,n).passed);
    n=ref; n.voltages["v(out)"][2]=std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(check.compare(ref,n).passed);
    n=ref; n.currents["i(internal)"]={1}; EXPECT_FALSE(check.compare(ref,n).passed);
    n=ref; n.time[2]=n.time[1]; EXPECT_FALSE(check.compare(ref,n).passed);
    n=ref; n.time.back()=29e-6; EXPECT_FALSE(check.compare(ref,n).passed);
    n=ref; n.time.front()=1e-9; EXPECT_FALSE(check.compare(ref,n).passed);
    n=ref; n.voltages["v(out)"].pop_back(); EXPECT_FALSE(check.compare(ref,n).passed);
}

TEST(Tlv3201Validation, UsesReferenceDenominatorAndOriginalPortFloor) {
    auto r=waveform(); auto n=r; Tlv3201Validation check;
    // 0.05025/5 fails1%; reversing reference and actual would incorrectly pass.
    n.voltages["v(vcc)"].assign(5,5.05025);
    EXPECT_FALSE(check.compare(r,n).passed);
    r.voltages["v(inm)"].assign(5,0); n=r;
    n.voltages["v(inm)"].assign(5,0.000099); EXPECT_TRUE(check.compare(r,n).passed);
    n.voltages["v(inm)"].assign(5,0.000101); EXPECT_FALSE(check.compare(r,n).passed);
}

TEST(Tlv3201Validation, MissingInputCannotReuseEarlierPassingEvidence) {
    Tlv3201Validation check; auto r=waveform(); ASSERT_TRUE(check.compare(r,r).passed);
    check.reset();
    EXPECT_TRUE(check.expected_edges.empty());
    EXPECT_FALSE(check.waveform.passed);
    Workload w{"tran_tlv3201","tlv3201_switching.cir","tran 100n 30u",Workload::TRAN,0,100e-9,30e-6};
    EXPECT_FALSE(check(Outputs{},Outputs{},w).passed);
    EXPECT_TRUE(check.actual_edges.empty());
}

TEST(Tlv3201Validation, FailedLaterPairCannotBecomeAcceptedTiming) {
    Tlv3201Validation check; const auto ref=waveform(); auto actual=ref;
    auto n=[&]{return std::pair{actual,bench::Phases{}};};
    auto r=[&]{return std::pair{ref,bench::Phases{}};};
    auto validate=[&](const auto& expected,const auto& value){return check.compare(expected,value);};
    EXPECT_NO_THROW(bench::paired(true,n,r,validate));
    actual.voltages["v(out)"].assign(5,0);
    EXPECT_THROW(bench::paired(false,n,r,validate),std::runtime_error);
}
