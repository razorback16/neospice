#include <gtest/gtest.h>
#include "parser/netlist_parser.hpp"
#include <filesystem>
#include <fstream>
#include <string>

using namespace neospice;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Test fixture: manages a temporary directory for test files
// ---------------------------------------------------------------------------
class IncludeTest : public ::testing::Test {
protected:
    fs::path tmp_dir_;

    void SetUp() override {
        // Create a unique temporary directory for each test
        tmp_dir_ = fs::temp_directory_path() / ("neospice_include_test_" +
                   std::to_string(reinterpret_cast<uintptr_t>(this)));
        fs::create_directories(tmp_dir_);
    }

    void TearDown() override {
        // Clean up temporary files
        fs::remove_all(tmp_dir_);
    }

    // Write a file in the test's temporary directory
    fs::path write_file(const std::string& name, const std::string& content) {
        fs::path p = tmp_dir_ / name;
        fs::create_directories(p.parent_path());
        std::ofstream ofs(p);
        ofs << content;
        return p;
    }
};

// ---------------------------------------------------------------------------
// 1. Basic include: main file includes subfile with a resistor
// ---------------------------------------------------------------------------
TEST_F(IncludeTest, BasicInclude) {
    write_file("sub.sp", "R1 a b 1k\n");

    fs::path main_path = write_file("main.sp",
        "Test netlist\n"
        ".include sub.sp\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    auto ckt = parser.parse_file(main_path.string());

    ASSERT_EQ(ckt.devices().size(), 1u);
    EXPECT_EQ(ckt.devices()[0]->name(), "R1");
}

// ---------------------------------------------------------------------------
// 2. Relative path: main at tmp/a/main.sp includes sub/models.sp
// ---------------------------------------------------------------------------
TEST_F(IncludeTest, RelativePath) {
    write_file("a/sub/models.sp", "R2 c d 2k\n");

    fs::path main_path = write_file("a/main.sp",
        "Test netlist\n"
        ".include sub/models.sp\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    auto ckt = parser.parse_file(main_path.string());

    ASSERT_EQ(ckt.devices().size(), 1u);
    EXPECT_EQ(ckt.devices()[0]->name(), "R2");
}

// ---------------------------------------------------------------------------
// 3. Absolute path include
// ---------------------------------------------------------------------------
TEST_F(IncludeTest, AbsolutePath) {
    fs::path sub_path = write_file("absolute_sub.sp", "R3 e f 3k\n");

    fs::path main_path = write_file("main_abs.sp",
        "Test netlist\n"
        ".include " + sub_path.string() + "\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    auto ckt = parser.parse_file(main_path.string());

    ASSERT_EQ(ckt.devices().size(), 1u);
    EXPECT_EQ(ckt.devices()[0]->name(), "R3");
}

// ---------------------------------------------------------------------------
// 4a. Quoted filename with double quotes
// ---------------------------------------------------------------------------
TEST_F(IncludeTest, QuotedFilenameDoubleQuotes) {
    write_file("quoted_sub.sp", "R4 g h 4k\n");

    fs::path main_path = write_file("main_dq.sp",
        "Test netlist\n"
        ".include \"quoted_sub.sp\"\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    auto ckt = parser.parse_file(main_path.string());

    ASSERT_EQ(ckt.devices().size(), 1u);
    EXPECT_EQ(ckt.devices()[0]->name(), "R4");
}

// ---------------------------------------------------------------------------
// 4b. Quoted filename with single quotes
// ---------------------------------------------------------------------------
TEST_F(IncludeTest, QuotedFilenameSingleQuotes) {
    write_file("single_quoted_sub.sp", "R5 i j 5k\n");

    fs::path main_path = write_file("main_sq.sp",
        "Test netlist\n"
        ".include 'single_quoted_sub.sp'\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    auto ckt = parser.parse_file(main_path.string());

    ASSERT_EQ(ckt.devices().size(), 1u);
    EXPECT_EQ(ckt.devices()[0]->name(), "R5");
}

// ---------------------------------------------------------------------------
// 5. Circular include: A includes B includes A
// ---------------------------------------------------------------------------
TEST_F(IncludeTest, CircularInclude) {
    // Write B first (references A which we write next)
    fs::path a_path = tmp_dir_ / "circ_a.sp";
    fs::path b_path = tmp_dir_ / "circ_b.sp";

    {
        std::ofstream fa(a_path);
        fa << "Test A\n"
           << ".include circ_b.sp\n"
           << ".end\n";
    }
    {
        std::ofstream fb(b_path);
        fb << ".include circ_a.sp\n";
    }

    NetlistParser parser;
    EXPECT_NO_THROW(parser.parse_file(a_path.string()));
}

// ---------------------------------------------------------------------------
// 6. Self-include: file includes itself
// ---------------------------------------------------------------------------
TEST_F(IncludeTest, SelfInclude) {
    fs::path self_path = tmp_dir_ / "self.sp";
    {
        std::ofstream f(self_path);
        f << "Test self\n"
          << ".include self.sp\n"
          << ".end\n";
    }

    NetlistParser parser;
    EXPECT_NO_THROW(parser.parse_file(self_path.string()));
}

// ---------------------------------------------------------------------------
// 7. Nested includes: A includes B includes C
// ---------------------------------------------------------------------------
TEST_F(IncludeTest, NestedIncludes) {
    // C has R_c, B includes C and has R_b, A (main) includes B and has R_a
    write_file("c.sp", "R_c k l 100\n");
    write_file("b.sp",
        ".include c.sp\n"
        "R_b m n 200\n");

    fs::path main_path = write_file("main_nested.sp",
        "Test nested\n"
        ".include b.sp\n"
        "R_a o p 300\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    auto ckt = parser.parse_file(main_path.string());

    // Should have R_c, R_b, and R_a
    ASSERT_EQ(ckt.devices().size(), 3u);

    std::set<std::string> names;
    for (const auto& dev : ckt.devices()) {
        names.insert(dev->name());
    }
    EXPECT_NE(names.find("R_c"), names.end());
    EXPECT_NE(names.find("R_b"), names.end());
    EXPECT_NE(names.find("R_a"), names.end());
}

// ---------------------------------------------------------------------------
// 8. Missing file: .include nonexistent.sp => ParseError
// ---------------------------------------------------------------------------
TEST_F(IncludeTest, MissingFile) {
    fs::path main_path = write_file("main_missing.sp",
        "Test netlist\n"
        ".include nonexistent_file_xyz.sp\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    EXPECT_NO_THROW(parser.parse_file(main_path.string()));
}

// ---------------------------------------------------------------------------
// 9a. Case insensitive: .INCLUDE
// ---------------------------------------------------------------------------
TEST_F(IncludeTest, CaseInsensitiveUppercase) {
    write_file("upper_sub.sp", "R6 q r 6k\n");

    fs::path main_path = write_file("main_upper.sp",
        "Test netlist\n"
        ".INCLUDE upper_sub.sp\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    auto ckt = parser.parse_file(main_path.string());

    ASSERT_EQ(ckt.devices().size(), 1u);
    EXPECT_EQ(ckt.devices()[0]->name(), "R6");
}

// ---------------------------------------------------------------------------
// 9b. Case insensitive: .Include (mixed case)
// ---------------------------------------------------------------------------
TEST_F(IncludeTest, CaseInsensitiveMixed) {
    write_file("mixed_sub.sp", "R7 s t 7k\n");

    fs::path main_path = write_file("main_mixed.sp",
        "Test netlist\n"
        ".Include mixed_sub.sp\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    auto ckt = parser.parse_file(main_path.string());

    ASSERT_EQ(ckt.devices().size(), 1u);
    EXPECT_EQ(ckt.devices()[0]->name(), "R7");
}

// ---------------------------------------------------------------------------
// 10. Include with subcircuit: included file contains .subckt/.ends,
//     main file uses X instance referencing it
// ---------------------------------------------------------------------------
TEST_F(IncludeTest, IncludeWithSubcircuit) {
    // Sub file defines a subcircuit "myres" with one resistor
    write_file("models.sp",
        ".subckt myres p n\n"
        "R1 p n 1k\n"
        ".ends myres\n");

    fs::path main_path = write_file("main_subckt.sp",
        "Test subckt include\n"
        ".include models.sp\n"
        "Xinst a b myres\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    auto ckt = parser.parse_file(main_path.string());

    // The X instance should be expanded into one resistor
    ASSERT_EQ(ckt.devices().size(), 1u);
}

// ---------------------------------------------------------------------------
// 11. Multiple includes in same file
// ---------------------------------------------------------------------------
TEST_F(IncludeTest, MultipleIncludes) {
    write_file("part1.sp", "R_p1 a b 1k\n");
    write_file("part2.sp", "R_p2 c d 2k\n");

    fs::path main_path = write_file("main_multi.sp",
        "Test multi include\n"
        ".include part1.sp\n"
        ".include part2.sp\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    auto ckt = parser.parse_file(main_path.string());

    ASSERT_EQ(ckt.devices().size(), 2u);
    std::set<std::string> names;
    for (const auto& dev : ckt.devices()) {
        names.insert(dev->name());
    }
    EXPECT_NE(names.find("R_p1"), names.end());
    EXPECT_NE(names.find("R_p2"), names.end());
}

// ---------------------------------------------------------------------------
// 12. Non-circular diamond: A includes B and C, both B and C include D
//     (same file included twice from different paths — allowed, not circular)
// ---------------------------------------------------------------------------
TEST_F(IncludeTest, DiamondIncludeAllowed) {
    // D defines a shared model/component
    write_file("shared.sp", "* shared definitions\n");
    write_file("left.sp",
        ".include shared.sp\n"
        "R_left a b 1k\n");
    write_file("right.sp",
        ".include shared.sp\n"
        "R_right c d 2k\n");

    fs::path main_path = write_file("main_diamond.sp",
        "Test diamond include\n"
        ".include left.sp\n"
        ".include right.sp\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    // Diamond (non-circular) includes should succeed
    EXPECT_NO_THROW({
        auto ckt = parser.parse_file(main_path.string());
        EXPECT_EQ(ckt.devices().size(), 2u);
    });
}
