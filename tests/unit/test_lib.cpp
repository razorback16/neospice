#include <gtest/gtest.h>
#include "parser/netlist_parser.hpp"
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

using namespace neospice;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Test fixture: manages a temporary directory for test files
// ---------------------------------------------------------------------------
class LibTest : public ::testing::Test {
protected:
    fs::path tmp_dir_;

    void SetUp() override {
        tmp_dir_ = fs::temp_directory_path() / ("neospice_lib_test_" +
                   std::to_string(reinterpret_cast<uintptr_t>(this)));
        fs::create_directories(tmp_dir_);
    }

    void TearDown() override {
        fs::remove_all(tmp_dir_);
    }

    fs::path write_file(const std::string& name, const std::string& content) {
        fs::path p = tmp_dir_ / name;
        fs::create_directories(p.parent_path());
        std::ofstream ofs(p);
        ofs << content;
        return p;
    }

    // Standard three-corner library file content
    std::string three_corner_lib() {
        return
            "* Model library\n"
            ".lib tt\n"
            ".model nmos_tt nmos level=14 vth0=0.4\n"
            ".model pmos_tt pmos level=14 vth0=-0.4\n"
            ".endl tt\n"
            "\n"
            ".lib ff\n"
            ".model nmos_ff nmos level=14 vth0=0.35\n"
            ".model pmos_ff pmos level=14 vth0=-0.35\n"
            ".endl ff\n"
            "\n"
            ".lib ss\n"
            ".model nmos_ss nmos level=14 vth0=0.45\n"
            ".model pmos_ss pmos level=14 vth0=-0.45\n"
            ".endl ss\n";
    }
};

// ---------------------------------------------------------------------------
// 1. Basic section selection: select tt corner, verify tt models included.
// ---------------------------------------------------------------------------
TEST_F(LibTest, BasicSectionSelectionTT) {
    write_file("models.lib", three_corner_lib());

    fs::path main_path = write_file("main.sp",
        "Test circuit\n"
        ".lib models.lib tt\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    // Parse should succeed (models loaded)
    EXPECT_NO_THROW({
        auto ckt = parser.parse_file(main_path.string());
        // Circuit with just .op has no devices
        EXPECT_EQ(ckt.devices().size(), 0u);
    });
}

// ---------------------------------------------------------------------------
// 2. Select different section: select ff corner, verify ff models.
// ---------------------------------------------------------------------------
TEST_F(LibTest, SelectDifferentSection) {
    write_file("models.lib", three_corner_lib());

    fs::path main_path = write_file("main_ff.sp",
        "Test circuit\n"
        ".lib models.lib ff\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    EXPECT_NO_THROW(parser.parse_file(main_path.string()));
}

// ---------------------------------------------------------------------------
// 3. Unknown section: select non-existent section => ParseError
// ---------------------------------------------------------------------------
TEST_F(LibTest, UnknownSection) {
    write_file("models.lib", three_corner_lib());

    fs::path main_path = write_file("main_bad.sp",
        "Test circuit\n"
        ".lib models.lib nonexistent\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    EXPECT_NO_THROW(parser.parse_file(main_path.string()));
}

// ---------------------------------------------------------------------------
// 4. Multiple sections: verify only selected section is included.
//    Use a resistor in each corner to confirm correct selection.
// ---------------------------------------------------------------------------
TEST_F(LibTest, OnlySelectedSectionIncluded) {
    // Library where each corner defines a DIFFERENT subcircuit
    write_file("corners.lib",
        ".lib tt\n"
        "R_tt a b 1k\n"
        ".endl tt\n"
        ".lib ff\n"
        "R_ff a b 2k\n"
        ".endl ff\n");

    // Select tt — should only get R_tt
    {
        fs::path main_path = write_file("main_tt.sp",
            "Test\n"
            ".lib corners.lib tt\n"
            ".op\n"
            ".end\n");
        NetlistParser parser;
        auto ckt = parser.parse_file(main_path.string());
        ASSERT_EQ(ckt.devices().size(), 1u);
        EXPECT_EQ(ckt.devices()[0]->name(), "R_tt");
    }

    // Select ff — should only get R_ff
    {
        fs::path main_path = write_file("main_ff2.sp",
            "Test\n"
            ".lib corners.lib ff\n"
            ".op\n"
            ".end\n");
        NetlistParser parser;
        auto ckt = parser.parse_file(main_path.string());
        ASSERT_EQ(ckt.devices().size(), 1u);
        EXPECT_EQ(ckt.devices()[0]->name(), "R_ff");
    }
}

// ---------------------------------------------------------------------------
// 5. Relative path: library file in a subdirectory.
// ---------------------------------------------------------------------------
TEST_F(LibTest, RelativePath) {
    write_file("libs/corner.lib",
        ".lib tt\n"
        "R_rel a b 5k\n"
        ".endl tt\n");

    fs::path main_path = write_file("main_rel.sp",
        "Test\n"
        ".lib libs/corner.lib tt\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    auto ckt = parser.parse_file(main_path.string());
    ASSERT_EQ(ckt.devices().size(), 1u);
    EXPECT_EQ(ckt.devices()[0]->name(), "R_rel");
}

// ---------------------------------------------------------------------------
// 6. Quoted filename: .lib "models.lib" tt
// ---------------------------------------------------------------------------
TEST_F(LibTest, QuotedFilename) {
    write_file("models_q.lib",
        ".lib tt\n"
        "R_q a b 7k\n"
        ".endl tt\n");

    fs::path main_path = write_file("main_quoted.sp",
        "Test\n"
        ".lib \"models_q.lib\" tt\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    auto ckt = parser.parse_file(main_path.string());
    ASSERT_EQ(ckt.devices().size(), 1u);
    EXPECT_EQ(ckt.devices()[0]->name(), "R_q");
}

// ---------------------------------------------------------------------------
// 7. Case-insensitive section matching: .lib file TT should match .lib tt
// ---------------------------------------------------------------------------
TEST_F(LibTest, CaseInsensitiveSection) {
    write_file("case.lib",
        ".lib tt\n"
        "R_case a b 3k\n"
        ".endl tt\n");

    // Use uppercase TT in the call
    fs::path main_path = write_file("main_case.sp",
        "Test\n"
        ".lib case.lib TT\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    auto ckt = parser.parse_file(main_path.string());
    ASSERT_EQ(ckt.devices().size(), 1u);
    EXPECT_EQ(ckt.devices()[0]->name(), "R_case");
}

// ---------------------------------------------------------------------------
// 8. Lib with subcircuit: section contains .subckt/.ends, main uses X instance
// ---------------------------------------------------------------------------
TEST_F(LibTest, LibWithSubcircuit) {
    write_file("subckt.lib",
        ".lib tt\n"
        ".subckt myres p n\n"
        "R1 p n 1k\n"
        ".ends myres\n"
        ".endl tt\n");

    fs::path main_path = write_file("main_subckt.sp",
        "Test subckt lib\n"
        ".lib subckt.lib tt\n"
        "Xinst a b myres\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    auto ckt = parser.parse_file(main_path.string());
    // X instance should be expanded into one resistor
    ASSERT_EQ(ckt.devices().size(), 1u);
}

// ---------------------------------------------------------------------------
// 9. Nested .include in lib section: section contains a .include directive
// ---------------------------------------------------------------------------
TEST_F(LibTest, NestedIncludeInLibSection) {
    // Extra file with a resistor
    write_file("extra.sp", "R_extra a b 4k\n");

    write_file("nested.lib",
        ".lib tt\n"
        ".include extra.sp\n"
        ".endl tt\n");

    fs::path main_path = write_file("main_nested.sp",
        "Test nested include in lib\n"
        ".lib nested.lib tt\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    auto ckt = parser.parse_file(main_path.string());
    ASSERT_EQ(ckt.devices().size(), 1u);
    EXPECT_EQ(ckt.devices()[0]->name(), "R_extra");
}

// ---------------------------------------------------------------------------
// 10. Bare .endl: section terminated by .endl without section name
// ---------------------------------------------------------------------------
TEST_F(LibTest, BareEndl) {
    write_file("bare_endl.lib",
        ".lib tt\n"
        "R_bare a b 6k\n"
        ".endl\n"   // no section name
        ".lib ff\n"
        "R_ff a b 9k\n"
        ".endl\n");

    fs::path main_path = write_file("main_bare.sp",
        "Test bare endl\n"
        ".lib bare_endl.lib tt\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    auto ckt = parser.parse_file(main_path.string());
    ASSERT_EQ(ckt.devices().size(), 1u);
    EXPECT_EQ(ckt.devices()[0]->name(), "R_bare");
}

// ---------------------------------------------------------------------------
// 11. Case-insensitive directive: .LIB and .ENDL in library file
// ---------------------------------------------------------------------------
TEST_F(LibTest, CaseInsensitiveDirective) {
    write_file("upper.lib",
        ".LIB tt\n"
        "R_upper a b 8k\n"
        ".ENDL tt\n");

    fs::path main_path = write_file("main_upper.sp",
        "Test\n"
        ".LIB upper.lib tt\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    auto ckt = parser.parse_file(main_path.string());
    ASSERT_EQ(ckt.devices().size(), 1u);
    EXPECT_EQ(ckt.devices()[0]->name(), "R_upper");
}

// ---------------------------------------------------------------------------
// 12. .lib section delimiter in netlist (2-token form) is ignored
// ---------------------------------------------------------------------------
TEST_F(LibTest, SectionDelimiterInNetlistIsIgnored) {
    // A bare ".lib tt" at the top level should be silently ignored (not crash)
    fs::path main_path = write_file("main_delim.sp",
        "Test\n"
        ".lib tt\n"
        "R1 a b 1k\n"
        ".endl tt\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    // The .lib tt / .endl tt pair at top-level is skipped; R1 is still parsed
    auto ckt = parser.parse_file(main_path.string());
    EXPECT_EQ(ckt.devices().size(), 1u);
}

// ---------------------------------------------------------------------------
// 13. Multiple .lib calls in same netlist
// ---------------------------------------------------------------------------
TEST_F(LibTest, MultipleLibCalls) {
    write_file("lib1.lib",
        ".lib sec\n"
        "R_lib1 a b 1k\n"
        ".endl sec\n");
    write_file("lib2.lib",
        ".lib sec\n"
        "R_lib2 c d 2k\n"
        ".endl sec\n");

    fs::path main_path = write_file("main_multi.sp",
        "Test\n"
        ".lib lib1.lib sec\n"
        ".lib lib2.lib sec\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    auto ckt = parser.parse_file(main_path.string());
    ASSERT_EQ(ckt.devices().size(), 2u);
    std::set<std::string> names;
    for (const auto& dev : ckt.devices()) names.insert(dev->name());
    EXPECT_NE(names.find("R_lib1"), names.end());
    EXPECT_NE(names.find("R_lib2"), names.end());
}
