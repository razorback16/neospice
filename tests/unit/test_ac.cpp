#include <gtest/gtest.h>
#include "core/ac.hpp"
#include "core/dc.hpp"
#include "parser/netlist_parser.hpp"
#include <cmath>
#include "core/freq_utils.hpp"
#include "framework/ngspice_lib.hpp"
#include <limits>

using namespace neospice;

TEST(AC, FailedDCMustNotSupplyCachedBias) {
    NetlistParser parser;
    auto ckt = parser.parse("No real operating point\nV1 in 0 1 AC 1\n"
        "B1 out 0 I={v(out)^2+v(out)+v(in)}\n.end\n");
    ckt.options.no_throw = true;
    const auto dc = solve_dc(ckt);
    ASSERT_FALSE(dc.status.converged);
    EXPECT_EQ(ckt.operating_point(), nullptr);
    const auto ac = solve_ac(ckt, ACMode::LIN, 1, 1e3, 1e3);
    EXPECT_FALSE(ac.status.converged);
    EXPECT_TRUE(ac.frequency.empty());
    EXPECT_TRUE(ac.voltages.empty());
}

TEST(AC, RCLowpass) {
    // RC lowpass: R=1k, C=1nF -> fc = 1/(2*pi*RC) ~ 159kHz
    std::string netlist = R"(
RC Lowpass
V1 in 0 DC 0 AC 1
R1 in out 1k
C1 out 0 1n
.ac dec 10 100 10meg
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_ac(ckt, AnalysisCommand::DEC, 10, 100.0, 10e6);

    EXPECT_FALSE(result.frequency.empty());
    EXPECT_FALSE(result.voltage("out").empty());

    // Find frequency closest to fc ~ 159kHz
    double fc = 1.0 / (2.0 * M_PI * 1e3 * 1e-9);
    int idx_fc = 0;
    double min_diff = 1e20;
    for (size_t i = 0; i < result.frequency.size(); ++i) {
        double diff = std::abs(result.frequency[i] - fc);
        if (diff < min_diff) { min_diff = diff; idx_fc = static_cast<int>(i); }
    }

    double mag_at_fc = std::abs(result.voltage("out")[idx_fc]);
    EXPECT_NEAR(mag_at_fc, 1.0 / std::sqrt(2.0), 0.05);

    // At low frequency: magnitude ~ 1
    EXPECT_NEAR(std::abs(result.voltage("out").front()), 1.0, 0.01);
}

TEST(AC, ResultHasFrequencyVector) {
    std::string netlist = R"(
Simple
V1 in 0 DC 0 AC 1
R1 in 0 1k
.ac dec 5 1 1meg
.end
)";
    NetlistParser parser;
    auto ckt = parser.parse(netlist);
    auto result = solve_ac(ckt, AnalysisCommand::DEC, 5, 1.0, 1e6);

    // DEC mode: 5 points per decade, 6 decades (1 to 1MHz) -> 30 points + 1
    EXPECT_GE(result.frequency.size(), 30u);
}

TEST(FrequencyGrid, MatchesNgspiceACAndNoiseRules) {
    struct Case { const char* command; ACMode mode; int points; double stop; bool noise; };
    for (const auto& c : {
        Case{".ac dec 10 1 3", ACMode::DEC, 10, 3, false},
        Case{".ac oct 4 1 3", ACMode::OCT, 4, 3, false},
        Case{".ac lin 2 1 3", ACMode::LIN, 2, 3, false},
        Case{".ac lin 7 1 3", ACMode::LIN, 7, 3, false},
        Case{".noise v(out) v1 dec 10 1 3", ACMode::DEC, 10, 3, true},
        Case{".noise v(out) v1 lin 2 1 3", ACMode::LIN, 2, 3, true}}) {
        SCOPED_TRACE(c.command);
        NgspiceLib ng;
        ng.reset();
        ng.load_circuit_lines({"Grid regression", "V1 in 0 AC 1", "R1 in out 1k",
                               "R2 out 0 1k", c.command, ".end"});
        ng.run();
        std::string plot;
        char** plots = ng.all_plots();
        ASSERT_NE(plots, nullptr);
        for (size_t i = 0; plots[i]; ++i) {
            char** names = ng.all_vecs(plots[i]);
            if (!names) continue;
            for (size_t j = 0; names[j]; ++j)
                if (std::string(names[j]) == "frequency") plot = plots[i];
        }
        ASSERT_FALSE(plot.empty());
        const auto* vec = ng.get_vec_info(plot + ".frequency");
        ASSERT_NE(vec, nullptr);
        const auto actual = generate_frequencies(c.mode, c.points, 1, c.stop, 1e-3,
            c.noise ? FrequencyAnalysis::Noise : FrequencyAnalysis::AC);
        ASSERT_EQ(actual.size(), static_cast<size_t>(vec->v_length));
        for (size_t i = 0; i < actual.size(); ++i)
            EXPECT_EQ(actual[i], vec->v_compdata ? vec->v_compdata[i].cx_real : vec->v_realdata[i]);
    }
}

TEST(FrequencyGrid, SingleAndNarrowDecadeSweepsRemainDefined) {
    // ngspice 47 explicitly defines single-point and narrow sweep ranges.
    // Neither should disappear from an AC result.
    EXPECT_EQ(generate_frequencies(ACMode::DEC, 10, 1, 1), std::vector<double>{1.0});
    EXPECT_EQ(generate_frequencies(ACMode::DEC, 1, 1, 1.01), std::vector<double>{1.0});
    const auto narrow = generate_frequencies(ACMode::DEC, 10, 1, 3);
    ASSERT_EQ(narrow.size(), 5u);
    EXPECT_NEAR(narrow[1], 1.2589254117941673, 1e-15);
    EXPECT_NEAR(narrow.back(), 2.5118864315095806, 1e-15);
}

TEST(FrequencyGrid, RejectsInvalidGrids) {
    EXPECT_TRUE(generate_frequencies(ACMode::LIN, 10, 1,
        std::numeric_limits<double>::infinity()).empty());
    EXPECT_TRUE(generate_frequencies(ACMode::LIN, 10, -1, 2).empty());
}
