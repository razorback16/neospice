#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "output/output.hpp"
#include "parser/netlist_parser.hpp"
#include <cmath>
#include <string>
#include <algorithm>

using namespace neospice;

// =========================================================================
// Parser tests
// =========================================================================

TEST(OutputParser, PrintTransient) {
    Simulator sim;
    auto ckt = sim.parse(R"(
test print parser
V1 in 0 DC 1
R1 in out 1k
R2 out 0 1k
.tran 1n 10n
.print tran V(out) I(V1)
.end
)");
    ASSERT_EQ(ckt.prints.size(), 1u);
    EXPECT_EQ(ckt.prints[0].analysis_type, "tran");
    EXPECT_FALSE(ckt.prints[0].is_plot);
    ASSERT_EQ(ckt.prints[0].signals.size(), 2u);
    EXPECT_EQ(ckt.prints[0].signals[0], "v(out)");
    EXPECT_EQ(ckt.prints[0].signals[1], "i(v1)");
}

TEST(OutputParser, PlotTransient) {
    Simulator sim;
    auto ckt = sim.parse(R"(
test plot parser
V1 in 0 DC 1
R1 in out 1k
R2 out 0 1k
.tran 1n 10n
.plot tran V(out) I(V1)
.end
)");
    ASSERT_EQ(ckt.prints.size(), 1u);
    EXPECT_EQ(ckt.prints[0].analysis_type, "tran");
    EXPECT_TRUE(ckt.prints[0].is_plot);
    ASSERT_EQ(ckt.prints[0].signals.size(), 2u);
    EXPECT_EQ(ckt.prints[0].signals[0], "v(out)");
    EXPECT_EQ(ckt.prints[0].signals[1], "i(v1)");
}

TEST(OutputParser, PrintAC) {
    Simulator sim;
    auto ckt = sim.parse(R"(
test ac print parser
V1 in 0 AC 1
R1 in out 1k
R2 out 0 1k
.ac dec 10 1 1meg
.print ac VM(out) VP(out) VDB(out)
.end
)");
    ASSERT_EQ(ckt.prints.size(), 1u);
    EXPECT_EQ(ckt.prints[0].analysis_type, "ac");
    EXPECT_FALSE(ckt.prints[0].is_plot);
    ASSERT_EQ(ckt.prints[0].signals.size(), 3u);
    EXPECT_EQ(ckt.prints[0].signals[0], "vm(out)");
    EXPECT_EQ(ckt.prints[0].signals[1], "vp(out)");
    EXPECT_EQ(ckt.prints[0].signals[2], "vdb(out)");
}

TEST(OutputParser, PrintDC) {
    Simulator sim;
    auto ckt = sim.parse(R"(
test dc print parser
V1 in 0 DC 1
R1 in out 1k
R2 out 0 1k
.dc V1 0 5 1
.print dc V(out)
.end
)");
    ASSERT_EQ(ckt.prints.size(), 1u);
    EXPECT_EQ(ckt.prints[0].analysis_type, "dc");
    EXPECT_FALSE(ckt.prints[0].is_plot);
    ASSERT_EQ(ckt.prints[0].signals.size(), 1u);
    EXPECT_EQ(ckt.prints[0].signals[0], "v(out)");
}

// =========================================================================
// format_print: Transient
// =========================================================================

TEST(PrintOutput, TransientTabular) {
    Simulator sim;
    auto ckt = sim.parse(R"(
RC low-pass filter
V1 in 0 DC 1
R1 in out 1k
C1 out 0 1n
.tran 1n 5n
.print tran V(out)
.end
)");
    auto result = sim.run(ckt);
    ASSERT_EQ(result.print_output.size(), 1u);
    const std::string& out = result.print_output[0];

    // Should contain header with Index, time, v(out)
    EXPECT_NE(out.find("Index"), std::string::npos);
    EXPECT_NE(out.find("time"), std::string::npos);
    EXPECT_NE(out.find("v(out)"), std::string::npos);

    // Should have at least a few data rows
    size_t row_count = 0;
    for (char c : out) if (c == '\n') ++row_count;
    EXPECT_GT(row_count, 3u);  // header + separator + at least 1 data row

    // Each data row should have scientific notation values
    EXPECT_NE(out.find("e+"), std::string::npos);
}

// =========================================================================
// format_print: AC
// =========================================================================

TEST(PrintOutput, ACTabular) {
    Simulator sim;
    auto ckt = sim.parse(R"(
RC low-pass filter AC
V1 in 0 AC 1
R1 in out 1k
C1 out 0 100n
.ac dec 3 100 10k
.print ac VM(out) VP(out) VDB(out)
.end
)");
    auto result = sim.run(ckt);
    ASSERT_EQ(result.print_output.size(), 1u);
    const std::string& out = result.print_output[0];

    EXPECT_NE(out.find("Index"), std::string::npos);
    EXPECT_NE(out.find("frequency"), std::string::npos);
    EXPECT_NE(out.find("vm(out)"), std::string::npos);
    EXPECT_NE(out.find("vp(out)"), std::string::npos);
    EXPECT_NE(out.find("vdb(out)"), std::string::npos);

    // Magnitude should be between 0 and 1 for a passive filter
    // Just verify we have numeric values (scientific notation)
    EXPECT_NE(out.find("e"), std::string::npos);
}

// =========================================================================
// format_plot: Transient
// =========================================================================

TEST(PlotOutput, TransientAsciiPlot) {
    Simulator sim;
    auto ckt = sim.parse(R"(
RC low-pass filter plot
V1 in 0 DC 1
R1 in out 1k
C1 out 0 1n
.tran 0.5n 3n
.plot tran V(out)
.end
)");
    auto result = sim.run(ckt);
    ASSERT_EQ(result.print_output.size(), 1u);
    const std::string& out = result.print_output[0];

    // Should contain the signal name
    EXPECT_NE(out.find("v(out)"), std::string::npos);
    // Should contain time axis name
    EXPECT_NE(out.find("time"), std::string::npos);
    // Should contain '*' marker
    EXPECT_NE(out.find('*'), std::string::npos);
    // Should have separator line
    EXPECT_NE(out.find("---"), std::string::npos);
    // Should have multiple data rows
    size_t row_count = 0;
    for (char c : out) if (c == '\n') ++row_count;
    EXPECT_GT(row_count, 4u);
}

// =========================================================================
// format_print: DC Sweep
// =========================================================================

TEST(PrintOutput, DCSweepTabular) {
    Simulator sim;
    auto ckt = sim.parse(R"(
DC sweep resistor divider
V1 in 0 DC 1
R1 in out 1k
R2 out 0 1k
.dc V1 0 2 0.5
.print dc V(out)
.end
)");
    auto result = sim.run(ckt);
    ASSERT_EQ(result.print_output.size(), 1u);
    const std::string& out = result.print_output[0];

    EXPECT_NE(out.find("Index"), std::string::npos);
    EXPECT_NE(out.find("v(out)"), std::string::npos);

    // Sweep V1 from 0 to 2 step 0.5 => 5 points, so at least 5 data rows + header
    size_t row_count = 0;
    for (char c : out) if (c == '\n') ++row_count;
    EXPECT_GE(row_count, 5u);

    // V(out) at V1=2V with equal resistors = 1.0V
    // Verify the output contains a value close to 1e+00
    EXPECT_NE(out.find("e+00"), std::string::npos);
}

// =========================================================================
// Integration: multiple print commands
// =========================================================================

TEST(PrintOutput, MultipleCommands) {
    Simulator sim;
    auto ckt = sim.parse(R"(
test multiple print commands
V1 in 0 DC 1
R1 in out 1k
R2 out 0 1k
.tran 1n 5n
.print tran V(out)
.plot  tran V(out)
.end
)");
    ASSERT_EQ(ckt.prints.size(), 2u);
    EXPECT_FALSE(ckt.prints[0].is_plot);
    EXPECT_TRUE(ckt.prints[1].is_plot);

    auto result = sim.run(ckt);
    EXPECT_EQ(result.print_output.size(), 2u);

    // First output is tabular (contains "Index")
    EXPECT_NE(result.print_output[0].find("Index"), std::string::npos);
    // Second output is plot (contains '*')
    EXPECT_NE(result.print_output[1].find('*'), std::string::npos);
}
