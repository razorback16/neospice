// Regression tests for PSpice AKO (.MODEL ... AKO: base) inheritance.
//
// These tests lock in two guarantees about the LAZY model-card path
// (ensure_model() in src/parser/netlist_parser.cpp):
//
//   1. CORRECTNESS of AKO resolution — simple inheritance, multi-level chains,
//      derived-overrides-base, type inheritance, missing base, circular
//      inheritance, and the instance-prefixed-base fallback. These must match
//      ngspice's behavior (ngspice is the reference).
//
//   2. LINEAR / LAZY behavior — a .model card is parsed at most once and only
//      when actually referenced. Resolving one model out of a large set does
//      not parse the unused cards. This is asserted DETERMINISTICALLY (no
//      timing): an unreferenced *malformed* .model card would throw if it were
//      parsed, so a clean parse proves it was never touched; and a very deep
//      AKO chain resolves to the correct leaf value, which would have been
//      O(depth^2) work under the old "passes over all models" scheme.

#include <gtest/gtest.h>
#include "parser/netlist_parser.hpp"
#include "parser/parse_state.hpp"  // DefinitionSet
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

using namespace neospice;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Fixture: writes temporary files so we can exercise both the file-based
// load_definitions() (which returns fully-resolved ModelCards) and parse_file().
// ---------------------------------------------------------------------------
class AKOTest : public ::testing::Test {
protected:
    fs::path tmp_dir_;

    void SetUp() override {
        tmp_dir_ = fs::temp_directory_path() / ("neospice_ako_test_" +
                   std::to_string(reinterpret_cast<uintptr_t>(this)));
        fs::create_directories(tmp_dir_);
    }
    void TearDown() override { fs::remove_all(tmp_dir_); }

    fs::path write_file(const std::string& name, const std::string& content) {
        fs::path p = tmp_dir_ / name;
        std::ofstream ofs(p);
        ofs << content;
        return p;
    }

    // load_definitions() returns a DefinitionSet whose `models` map holds the
    // fully parsed + AKO-resolved ModelCards (it forces materialize_all_models).
    DefinitionSet load(const std::string& content) {
        fs::path p = write_file("models.lib", content);
        NetlistParser parser;
        return parser.load_definitions(p.string());
    }
};

// Helper: does the circuit contain a device whose name matches `want`
// case-insensitively? (Device names keep their source case, e.g. "D1".)
static bool has_device(const Circuit& ckt, const std::string& want) {
    auto lc = [](std::string s) {
        for (auto& c : s) c = char(std::tolower((unsigned char)c));
        return s;
    };
    std::string w = lc(want);
    for (const auto& dev : ckt.devices())
        if (lc(dev->name()).find(w) != std::string::npos) return true;
    return false;
}

// Helper: fetch a resolved model param value. ModelCard param keys are lowercase.
static double param_of(const DefinitionSet& defs, const std::string& model,
                       const std::string& key) {
    auto mit = defs.models.find(model);
    EXPECT_NE(mit, defs.models.end()) << "model '" << model << "' not resolved";
    if (mit == defs.models.end()) return std::numeric_limits<double>::quiet_NaN();
    auto pit = mit->second.params.find(key);
    EXPECT_NE(pit, mit->second.params.end())
        << "param '" << key << "' missing on model '" << model << "'";
    if (pit == mit->second.params.end()) return std::numeric_limits<double>::quiet_NaN();
    return pit->second;
}

// ---------------------------------------------------------------------------
// 1. Simple AKO: derived inherits base params, overrides win, type inherited.
//    .MODEL FASTD AKO: BASED D(IS=2n RS=0.1)  from  .MODEL BASED D(IS=1n RS=8 N=1.5)
// ---------------------------------------------------------------------------
TEST_F(AKOTest, SimpleInheritanceAndOverride) {
    auto defs = load(
        "* simple AKO\n"
        ".MODEL BASED D(IS=1n RS=8 N=1.5)\n"
        ".MODEL FASTD AKO: BASED D(IS=2n RS=0.1)\n");

    // Base unchanged.
    EXPECT_NEAR(param_of(defs, "BASED", "is"), 1e-9, 1e-21);
    EXPECT_NEAR(param_of(defs, "BASED", "rs"), 8.0, 1e-12);

    // Derived: overridden params win, non-overridden inherited from base.
    EXPECT_NEAR(param_of(defs, "FASTD", "is"), 2e-9, 1e-21);   // overridden
    EXPECT_NEAR(param_of(defs, "FASTD", "rs"), 0.1, 1e-12);    // overridden
    EXPECT_NEAR(param_of(defs, "FASTD", "n"), 1.5, 1e-12);     // inherited

    // Type inherited (derived declared D too; equal either way).
    EXPECT_EQ(defs.models.at("FASTD").type, "d");
}

// ---------------------------------------------------------------------------
// 2. Multi-level chain: A AKO B, B AKO C. Deepest base value propagates,
//    each level's overrides win over ancestors.
// ---------------------------------------------------------------------------
TEST_F(AKOTest, MultiLevelChain) {
    auto defs = load(
        "* chain A->B->C\n"
        ".MODEL C D(IS=1n RS=10 N=1 BV=100)\n"
        ".MODEL B AKO: C D(RS=5 N=2)\n"
        ".MODEL A AKO: B D(N=3)\n");

    // A: N from A, RS from B, IS+BV from C.
    EXPECT_NEAR(param_of(defs, "A", "n"), 3.0, 1e-12);    // A overrides
    EXPECT_NEAR(param_of(defs, "A", "rs"), 5.0, 1e-12);   // from B
    EXPECT_NEAR(param_of(defs, "A", "is"), 1e-9, 1e-21);  // from C
    EXPECT_NEAR(param_of(defs, "A", "bv"), 100.0, 1e-9);  // from C

    // B: N+RS from B, IS+BV from C.
    EXPECT_NEAR(param_of(defs, "B", "n"), 2.0, 1e-12);
    EXPECT_NEAR(param_of(defs, "B", "rs"), 5.0, 1e-12);
    EXPECT_NEAR(param_of(defs, "B", "bv"), 100.0, 1e-9);
}

// ---------------------------------------------------------------------------
// 3. Type inheritance when derived omits the type token entirely.
//    PSpice allows ".MODEL NEWQ AKO: ORIGQ (BF=60)" with no explicit type;
//    the type must be taken from the base.
// ---------------------------------------------------------------------------
TEST_F(AKOTest, TypeInheritedWhenDerivedOmitsType) {
    auto defs = load(
        "* derived omits type\n"
        ".MODEL ORIGQ NPN(BF=100 IS=1e-16)\n"
        ".MODEL NEWQ AKO: ORIGQ (BF=60)\n");

    EXPECT_EQ(defs.models.at("NEWQ").type, "npn");           // inherited type
    EXPECT_NEAR(param_of(defs, "NEWQ", "bf"), 60.0, 1e-9);   // overridden
    EXPECT_NEAR(param_of(defs, "NEWQ", "is"), 1e-16, 1e-28); // inherited
}

// ---------------------------------------------------------------------------
// 4. Missing base: warn, and the derived model is still usable WITHOUT
//    inheritance (its own params survive, ako_base cleared).
// ---------------------------------------------------------------------------
TEST_F(AKOTest, MissingBaseUsesDerivedWithoutInheritance) {
    auto defs = load(
        "* missing base\n"
        ".MODEL ORPHAN AKO: NOSUCHBASE D(IS=3n RS=2)\n");

    auto mit = defs.models.find("ORPHAN");
    ASSERT_NE(mit, defs.models.end());
    EXPECT_EQ(mit->second.type, "d");
    EXPECT_TRUE(mit->second.ako_base.empty());            // link dropped
    EXPECT_NEAR(param_of(defs, "ORPHAN", "is"), 3e-9, 1e-21);
    EXPECT_NEAR(param_of(defs, "ORPHAN", "rs"), 2.0, 1e-12);
}

// ---------------------------------------------------------------------------
// 5. Circular inheritance: A AKO B, B AKO A. Must not hang/crash; the cycle
//    is broken and both models remain resolvable with their own params.
// ---------------------------------------------------------------------------
TEST_F(AKOTest, CircularInheritanceNoHang) {
    auto defs = load(
        "* circular\n"
        ".MODEL A AKO: B D(IS=1n)\n"
        ".MODEL B AKO: A D(IS=2n)\n");

    // Both still materialize (cycle guard breaks the loop); own params survive.
    ASSERT_NE(defs.models.find("A"), defs.models.end());
    ASSERT_NE(defs.models.find("B"), defs.models.end());
    EXPECT_EQ(defs.models.at("A").type, "d");
    EXPECT_EQ(defs.models.at("B").type, "d");
    EXPECT_NEAR(param_of(defs, "A", "is"), 1e-9, 1e-21);
    EXPECT_NEAR(param_of(defs, "B", "is"), 2e-9, 1e-21);
}

// ---------------------------------------------------------------------------
// 6. Instance-prefixed-base fallback. Inside a subckt the AKO base resolves to
//    the same-scope model after subcircuit expansion prefixes names (x1.qp's
//    base "qon" -> "x1.qon"). Mirrors ngspice's subckt-scope-first AKO search.
// ---------------------------------------------------------------------------
TEST_F(AKOTest, InstancePrefixedBaseFallback) {
    auto path = write_file("cross_scope.cir",
        "AKO cross-scope test\n"
        ".subckt mybjt C B E\n"
        ".model QON NPN(BF=100 IS=1e-14)\n"
        ".model QP AKO:QON NPN(BF=200)\n"
        "Q1 C B E QP\n"
        ".ends\n"
        "X1 col base 0 mybjt\n"
        "VCC col 0 5\n"
        "VBB base 0 0.7\n"
        ".op\n"
        ".end\n");

    NetlistParser parser;
    auto ckt = parser.parse_file(path.string());

    bool has_bjt = false;
    for (const auto& dev : ckt.devices()) {
        if (dev->name().find("q1") != std::string::npos) has_bjt = true;
    }
    EXPECT_TRUE(has_bjt) << "expected Q1 (model QP AKO x1.qon) to be instantiated";
}

// ---------------------------------------------------------------------------
// 7. LAZINESS / LINEARITY (deterministic, no timing).
//
//    A library defines many models. Exactly ONE is referenced by an element.
//    One of the UNREFERENCED cards is deliberately malformed such that
//    parse_model_card() would throw if it were ever parsed (".model X" has only
//    two tokens; parse_model_card requires >= 3). Because parse() is lazy and
//    only materializes referenced models (and their AKO bases), the malformed
//    unreferenced card is never parsed, so parse() must succeed.
//
//    Under any scheme that parses ALL .model cards (the old eager path), this
//    netlist would throw. A clean parse is therefore proof that unused models
//    are not parsed.
// ---------------------------------------------------------------------------
TEST_F(AKOTest, UnreferencedModelsNotParsed) {
    std::string nl =
        "Lazy model parse\n"
        ".model GOODD D(IS=1n)\n";
    // Many unused, well-formed decoys.
    for (int i = 0; i < 50; ++i) {
        nl += ".model UNUSED" + std::to_string(i) + " D(IS=" +
              std::to_string(i + 1) + "n)\n";
    }
    // An unused MALFORMED card: too few tokens => parse_model_card throws.
    nl += ".model BROKENMODEL\n";
    // Only GOODD is referenced.
    nl +=
        "V1 a 0 1\n"
        "D1 a 0 GOODD\n"
        ".op\n"
        ".end\n";

    fs::path p = write_file("lazy.cir", nl);
    NetlistParser parser;
    // Must NOT throw: the malformed BROKENMODEL is never referenced, so it is
    // never parsed.
    Circuit ckt = parser.parse_file(p.string());
    EXPECT_TRUE(has_device(ckt, "d1"));
}

// ---------------------------------------------------------------------------
// 8. Deep AKO chain resolves correctly. A 300-deep chain where only the leaf is
//    referenced. Correct leaf resolution proves transitive O(depth) resolution;
//    the old "models.size()+1 passes over all models" scheme was O(depth^2).
//    Each level overrides one param and inherits the rest; the leaf must carry
//    the base's deepest inherited value plus every level's own override of N.
// ---------------------------------------------------------------------------
TEST_F(AKOTest, DeepChainResolvesToLeaf) {
    const int depth = 300;
    std::string nl = "Deep AKO chain\n";
    // Base M0 carries a unique inherited param IS that must survive to the leaf.
    nl += ".model M0 D(IS=1n RS=0)\n";
    // M(k) AKO M(k-1), each overriding RS to its index. Leaf RS == depth-1.
    for (int k = 1; k < depth; ++k) {
        nl += ".model M" + std::to_string(k) + " AKO: M" + std::to_string(k - 1) +
              " D(RS=" + std::to_string(k) + ")\n";
    }
    // Only the leaf is referenced.
    nl +=
        "V1 a 0 1\n"
        "D1 a 0 M" + std::to_string(depth - 1) + "\n"
        ".op\n"
        ".end\n";

    fs::path p = write_file("deep.cir", nl);
    NetlistParser parser;
    Circuit ckt = parser.parse_file(p.string());
    EXPECT_TRUE(has_device(ckt, "d1")) << "leaf diode of deep AKO chain must resolve";

    // Also verify, via the resolved-model path, that the deepest base param IS
    // is inherited all the way to the leaf and the leaf RS override wins.
    auto defs = load(nl);
    std::string leaf = "M" + std::to_string(depth - 1);
    EXPECT_NEAR(param_of(defs, leaf, "is"), 1e-9, 1e-21);              // from M0
    EXPECT_NEAR(param_of(defs, leaf, "rs"), double(depth - 1), 1e-9); // leaf override
}
