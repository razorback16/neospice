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

TEST(Tokenizer, Comments) {
    auto lines = tokenize("* This is a comment\nR1 net1 0 1k\n");
    ASSERT_EQ(lines.size(), 1u);
}

TEST(Tokenizer, InlineComment) {
    auto lines = tokenize("R1 net1 0 1k $ this is inline\n");
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].tokens.size(), 4u);
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
