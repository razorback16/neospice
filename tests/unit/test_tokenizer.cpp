#include <gtest/gtest.h>
#include "parser/tokenizer.hpp"

using namespace neospice;

TEST(Tokenizer, SimpleLine) {
    auto lines = tokenize("R1 net1 0 1k\n");
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].tokens.size(), 4u);
    EXPECT_EQ(lines[0].tokens[0], "R1");
    EXPECT_EQ(lines[0].tokens[3], "1k");
}

TEST(Tokenizer, NumericSuffixes) {
    EXPECT_DOUBLE_EQ(parse_spice_number("1k"), 1e3);
    EXPECT_DOUBLE_EQ(parse_spice_number("2.2meg"), 2.2e6);
    EXPECT_DOUBLE_EQ(parse_spice_number("100u"), 100e-6);
    EXPECT_DOUBLE_EQ(parse_spice_number("47n"), 47e-9);
    EXPECT_DOUBLE_EQ(parse_spice_number("10p"), 10e-12);
    EXPECT_DOUBLE_EQ(parse_spice_number("1f"), 1e-15);
    EXPECT_DOUBLE_EQ(parse_spice_number("1e-3"), 1e-3);
    EXPECT_DOUBLE_EQ(parse_spice_number("3.3"), 3.3);
}

TEST(Tokenizer, LineContinuation) {
    auto lines = tokenize("R1 net1\n+ 0 1k\n");
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].tokens.size(), 4u);
}

// SPICE allows leading whitespace before the '+' continuation marker; vendor
// libraries frequently indent continuation lines. The tokenizer must look past
// the indentation, not at raw_line[0].
TEST(Tokenizer, IndentedLineContinuation) {
    auto lines = tokenize("R1 net1\n        + 0 1k\n");
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].tokens.size(), 4u);
}

// Leading whitespace before a '*' comment must also be recognised.
TEST(Tokenizer, IndentedComment) {
    auto lines = tokenize("    * indented comment\nR1 net1 0 1k\n");
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].tokens.size(), 4u);
}

TEST(Tokenizer, Comments) {
    auto lines = tokenize("* This is a comment\nR1 net1 0 1k\n");
    ASSERT_EQ(lines.size(), 1u);
}

TEST(Tokenizer, InlineCommentSemicolon) {
    // ';' is an inline comment anywhere on the line (ngspice, all modes).
    auto lines = tokenize("R1 net1 0 1k ; this is inline\n");
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].tokens.size(), 4u);
}

TEST(Tokenizer, DollarLineStartComment) {
    // Under ngbehavior=psa (the mode the comparison harness runs, and the one
    // neospice targets), '$' begins a comment ONLY when it is the first token
    // of the line. A mid-line / trailing space-separated '$' is a valid
    // identifier char (PSpice auto-named nodes like "$N_0002") and is NOT a
    // comment — ngspice-psa errors on a bare trailing "$" rather than stripping
    // it. So the trailing tokens must survive.
    auto c = tokenize("$ this whole line is a comment\nR1 a b 1k\n");
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0].tokens[0], "R1");

    auto node = tokenize("* t\nR1 a $N_0002 1k\n");
    ASSERT_EQ(node.size(), 1u);
    EXPECT_EQ(node[0].tokens.size(), 4u);
    EXPECT_EQ(node[0].tokens[2], "$N_0002");
}

TEST(Tokenizer, DotCommand) {
    auto lines = tokenize(".tran 1u 1m\n");
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].tokens[0], ".tran");
}

TEST(Tokenizer, TitleLine) {
    auto lines = tokenize("My Circuit\nR1 a b 1k\n.end\n");
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].tokens[0], "R1");
}

TEST(Tokenizer, CaseInsensitive) {
    auto lines = tokenize("r1 NET1 GND 1K\n");
    ASSERT_EQ(lines.size(), 1u);
}
