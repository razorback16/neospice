#include <gtest/gtest.h>
#include "parser/expression.hpp"
#include "core/types.hpp"
#include <cmath>
#include <unordered_map>
#include <vector>
#include <utility>

using namespace neospice;

// ============================================================
// Helpers
// ============================================================
static const std::unordered_map<std::string, double> NO_PARAMS;

static double eval(const std::string& expr,
                   const std::unordered_map<std::string, double>& params = NO_PARAMS) {
    return eval_expression(expr, params);
}

// ============================================================
// Basic arithmetic
// ============================================================
TEST(Expression, Addition) {
    EXPECT_DOUBLE_EQ(eval("1 + 2"), 3.0);
    EXPECT_DOUBLE_EQ(eval("1.5 + 2.5"), 4.0);
}

TEST(Expression, Subtraction) {
    EXPECT_DOUBLE_EQ(eval("5 - 3"), 2.0);
    EXPECT_DOUBLE_EQ(eval("3 - 5"), -2.0);
}

TEST(Expression, Multiplication) {
    EXPECT_DOUBLE_EQ(eval("3 * 4"), 12.0);
    EXPECT_DOUBLE_EQ(eval("2.5 * 4"), 10.0);
}

TEST(Expression, Division) {
    EXPECT_DOUBLE_EQ(eval("10 / 4"), 2.5);
    EXPECT_DOUBLE_EQ(eval("1 / 3"), 1.0 / 3.0);
}

TEST(Expression, UnaryMinus) {
    EXPECT_DOUBLE_EQ(eval("-3"), -3.0);
    EXPECT_DOUBLE_EQ(eval("--3"), 3.0);
    EXPECT_DOUBLE_EQ(eval("2 * -3"), -6.0);
}

TEST(Expression, UnaryPlus) {
    EXPECT_DOUBLE_EQ(eval("+5"), 5.0);
}

TEST(Expression, Parentheses) {
    EXPECT_DOUBLE_EQ(eval("(2 + 3) * 4"), 20.0);
    EXPECT_DOUBLE_EQ(eval("2 * (3 + 4)"), 14.0);
    EXPECT_DOUBLE_EQ(eval("((2 + 3))"), 5.0);
}

TEST(Expression, MixedArithmetic) {
    EXPECT_DOUBLE_EQ(eval("2 + 3 * 4"), 14.0);
    EXPECT_DOUBLE_EQ(eval("10 - 2 * 3"), 4.0);
    EXPECT_DOUBLE_EQ(eval("10 / 2 + 3"), 8.0);
}

// ============================================================
// Exponentiation (**)
// ============================================================
TEST(Expression, Exponentiation) {
    EXPECT_DOUBLE_EQ(eval("2 ** 3"), 8.0);
    EXPECT_DOUBLE_EQ(eval("2 ** 10"), 1024.0);
    EXPECT_DOUBLE_EQ(eval("4 ** 0.5"), 2.0);
    EXPECT_DOUBLE_EQ(eval("9 ** 0.5"), 3.0);
}

TEST(Expression, ExponentiationRightAssociative) {
    // 2 ** 3 ** 2 should be 2 ** (3 ** 2) = 2 ** 9 = 512
    EXPECT_DOUBLE_EQ(eval("2 ** 3 ** 2"), 512.0);
}

TEST(Expression, ExponentiationPrecedence) {
    // 2 * 3 ** 2 = 2 * 9 = 18 (** binds tighter than *)
    EXPECT_DOUBLE_EQ(eval("2 * 3 ** 2"), 18.0);
    // -2 ** 2: unary minus has lower precedence than **, so -(2**2) = -4
    EXPECT_DOUBLE_EQ(eval("-(2 ** 2)"), -4.0);
}

// ============================================================
// Built-in functions
// ============================================================
TEST(Expression, FuncSqrt) {
    EXPECT_NEAR(eval("sqrt(4)"), 2.0, 1e-12);
    EXPECT_NEAR(eval("sqrt(2)"), std::sqrt(2.0), 1e-12);
}

TEST(Expression, FuncAbs) {
    EXPECT_DOUBLE_EQ(eval("abs(-5)"), 5.0);
    EXPECT_DOUBLE_EQ(eval("abs(3.14)"), 3.14);
}

TEST(Expression, FuncLog) {
    EXPECT_NEAR(eval("log(1)"), 0.0, 1e-12);
    EXPECT_NEAR(eval("log(2.718281828459045)"), 1.0, 1e-10);
    EXPECT_NEAR(eval("exp(log(7))"), 7.0, 1e-10);
}

TEST(Expression, FuncLog10) {
    EXPECT_NEAR(eval("log10(100)"), 2.0, 1e-12);
    EXPECT_NEAR(eval("log10(1000)"), 3.0, 1e-12);
}

TEST(Expression, FuncExp) {
    EXPECT_NEAR(eval("exp(0)"), 1.0, 1e-12);
    EXPECT_NEAR(eval("exp(1)"), std::exp(1.0), 1e-12);
}

TEST(Expression, FuncSin) {
    EXPECT_NEAR(eval("sin(0)"), 0.0, 1e-12);
    EXPECT_NEAR(eval("sin(3.141592653589793)"), 0.0, 1e-12);
}

TEST(Expression, FuncCos) {
    EXPECT_NEAR(eval("cos(0)"), 1.0, 1e-12);
    EXPECT_NEAR(eval("cos(3.141592653589793)"), -1.0, 1e-12);
}

TEST(Expression, FuncMin) {
    EXPECT_DOUBLE_EQ(eval("min(3, 5)"), 3.0);
    EXPECT_DOUBLE_EQ(eval("min(5, 3)"), 3.0);
    EXPECT_DOUBLE_EQ(eval("min(-1, 0)"), -1.0);
}

TEST(Expression, FuncMax) {
    EXPECT_DOUBLE_EQ(eval("max(3, 5)"), 5.0);
    EXPECT_DOUBLE_EQ(eval("max(5, 3)"), 5.0);
    EXPECT_DOUBLE_EQ(eval("max(-1, 0)"), 0.0);
}

TEST(Expression, FuncPow) {
    EXPECT_NEAR(eval("pow(2, 10)"), 1024.0, 1e-9);
    EXPECT_NEAR(eval("pow(4, 0.5)"), 2.0, 1e-12);
}

// ============================================================
// Conditional: if()
// ============================================================
TEST(Expression, IfTrue) {
    // cond > 0 => true branch
    EXPECT_DOUBLE_EQ(eval("if(1, 10, 20)"), 10.0);
    EXPECT_DOUBLE_EQ(eval("if(0.5, 10, 20)"), 10.0);
}

TEST(Expression, IfFalse) {
    EXPECT_DOUBLE_EQ(eval("if(0, 10, 20)"), 20.0);
    EXPECT_DOUBLE_EQ(eval("if(-1, 10, 20)"), 20.0);
}

TEST(Expression, IfWithExpression) {
    // if(x > 0, ...) — expressed as if(x, ...) where x is evaluated
    std::unordered_map<std::string, double> p = {{"x", 5.0}};
    EXPECT_DOUBLE_EQ(eval("if(x, 1, -1)", p), 1.0);
    p["x"] = 0.0;
    EXPECT_DOUBLE_EQ(eval("if(x, 1, -1)", p), -1.0);
}

// ============================================================
// Brace stripping
// ============================================================
TEST(Expression, BraceStripping) {
    EXPECT_DOUBLE_EQ(eval("{2+3}"), 5.0);
    EXPECT_DOUBLE_EQ(eval("{10*2}"), 20.0);
    std::unordered_map<std::string, double> p = {{"r", 100.0}};
    EXPECT_DOUBLE_EQ(eval("{r * 2}", p), 200.0);
}

TEST(Expression, NoBraces) {
    EXPECT_DOUBLE_EQ(eval("2+3"), 5.0);
}

// ============================================================
// SPICE number suffixes in expressions
// ============================================================
TEST(Expression, SpiceSuffixes) {
    EXPECT_DOUBLE_EQ(eval("1k"), 1e3);
    EXPECT_NEAR(eval("2.2meg"), 2.2e6, 1.0);
    EXPECT_DOUBLE_EQ(eval("100u"), 100e-6);
    EXPECT_DOUBLE_EQ(eval("47n"), 47e-9);
}

// ============================================================
// Parameter cross-reference
// ============================================================
TEST(Expression, ParamLookup) {
    std::unordered_map<std::string, double> p = {{"x", 3.0}, {"y", 4.0}};
    EXPECT_DOUBLE_EQ(eval("x + y", p), 7.0);
    EXPECT_DOUBLE_EQ(eval("x * y", p), 12.0);
}

TEST(Expression, ParamInFunction) {
    std::unordered_map<std::string, double> p = {{"r", 9.0}};
    EXPECT_NEAR(eval("sqrt(r)", p), 3.0, 1e-12);
}

TEST(Expression, UnknownParamThrows) {
    EXPECT_THROW(eval("unknown_param"), ParseError);
}

// ============================================================
// resolve_params: topological sort
// ============================================================
TEST(ResolveParams, SimpleForwardOrder) {
    std::vector<std::pair<std::string, std::string>> raw = {
        {"x", "1e-6"},
        {"y", "2*x"},
    };
    auto result = resolve_params(raw);
    EXPECT_NEAR(result.at("x"), 1e-6, 1e-20);
    EXPECT_NEAR(result.at("y"), 2e-6, 1e-20);
}

TEST(ResolveParams, BackwardReference) {
    // y defined before x — topological sort should fix this
    std::vector<std::pair<std::string, std::string>> raw = {
        {"y", "2*x"},
        {"x", "1e-6"},
    };
    auto result = resolve_params(raw);
    EXPECT_NEAR(result.at("x"), 1e-6, 1e-20);
    EXPECT_NEAR(result.at("y"), 2e-6, 1e-20);
}

TEST(ResolveParams, ChainedDependencies) {
    std::vector<std::pair<std::string, std::string>> raw = {
        {"a", "2"},
        {"b", "a * 3"},
        {"c", "b + a"},   // c = 6 + 2 = 8
    };
    auto result = resolve_params(raw);
    EXPECT_DOUBLE_EQ(result.at("a"), 2.0);
    EXPECT_DOUBLE_EQ(result.at("b"), 6.0);
    EXPECT_DOUBLE_EQ(result.at("c"), 8.0);
}

TEST(ResolveParams, BracedExpression) {
    std::vector<std::pair<std::string, std::string>> raw = {
        {"x", "1e-6"},
        {"y", "{2*x}"},
    };
    auto result = resolve_params(raw);
    EXPECT_NEAR(result.at("y"), 2e-6, 1e-20);
}

TEST(ResolveParams, CircularDependency) {
    std::vector<std::pair<std::string, std::string>> raw = {
        {"a", "b + 1"},
        {"b", "a + 1"},
    };
    EXPECT_THROW(resolve_params(raw), ParseError);
}

TEST(ResolveParams, SelfCircular) {
    std::vector<std::pair<std::string, std::string>> raw = {
        {"a", "a + 1"},
    };
    EXPECT_THROW(resolve_params(raw), ParseError);
}

TEST(ResolveParams, Literals) {
    std::vector<std::pair<std::string, std::string>> raw = {
        {"pi", "3.141592653589793"},
        {"e",  "2.718281828459045"},
    };
    auto result = resolve_params(raw);
    EXPECT_NEAR(result.at("pi"), 3.141592653589793, 1e-12);
    EXPECT_NEAR(result.at("e"),  2.718281828459045, 1e-12);
}

TEST(ResolveParams, MultipleRoots) {
    // Two independent params + one that depends on both
    std::vector<std::pair<std::string, std::string>> raw = {
        {"w", "l * 2"},
        {"l", "100n"},
        {"area", "w * l"},
    };
    auto result = resolve_params(raw);
    EXPECT_DOUBLE_EQ(result.at("l"), 100e-9);
    EXPECT_DOUBLE_EQ(result.at("w"), 200e-9);
    // area = w*l: use relative tolerance
    EXPECT_NEAR(result.at("area") / (200e-9 * 100e-9), 1.0, 1e-10);
}

// ============================================================
// PDK-style parameter chains
// ============================================================
TEST(ResolveParams, PDKStyleChain) {
    // Typical PDK model card parameters
    std::vector<std::pair<std::string, std::string>> raw = {
        {"toxe",  "1.8e-9"},
        {"epsox", "{3.9 * 8.854e-12}"},
        {"cox",   "{epsox / toxe}"},
    };
    auto result = resolve_params(raw);
    double toxe  = 1.8e-9;
    double epsox = 3.9 * 8.854e-12;
    double cox   = epsox / toxe;
    EXPECT_NEAR(result.at("toxe"),  toxe,  1e-20);
    EXPECT_NEAR(result.at("epsox"), epsox, 1e-30);
    EXPECT_NEAR(result.at("cox"),   cox,   1e3); // ~1.9 mF/m^2 range, use relative
    EXPECT_NEAR(result.at("cox") / cox, 1.0, 1e-10);
}

TEST(ResolveParams, PDKStyleWithFunctions) {
    // Parameters using built-in functions
    std::vector<std::pair<std::string, std::string>> raw = {
        {"vth0",  "0.4"},
        {"m0",    "1.0"},
        {"dvtoff", "{abs(vth0) * 0.5}"},
        {"kappa", "{sqrt(m0)}"},
    };
    auto result = resolve_params(raw);
    EXPECT_NEAR(result.at("dvtoff"), 0.2, 1e-12);
    EXPECT_NEAR(result.at("kappa"),  1.0, 1e-12);
}

TEST(ResolveParams, PDKStyleBackwardRef) {
    // cox defined before tox in the list — must still work
    std::vector<std::pair<std::string, std::string>> raw = {
        {"cox",  "{epsox / tox}"},
        {"epsox","3.45e-11"},
        {"tox",  "1.5e-9"},
    };
    auto result = resolve_params(raw);
    double expected_cox = 3.45e-11 / 1.5e-9;
    EXPECT_NEAR(result.at("cox") / expected_cox, 1.0, 1e-10);
}

// ============================================================
// Case-insensitive function names
// ============================================================
TEST(Expression, FuncCaseInsensitive) {
    // Function names should be accepted in any case
    EXPECT_NEAR(eval("SQRT(4)"), 2.0, 1e-12);
    EXPECT_NEAR(eval("Sqrt(4)"), 2.0, 1e-12);
}

// ============================================================
// Error cases
// ============================================================
TEST(Expression, MissingCloseParen) {
    EXPECT_THROW(eval("(2 + 3"), ParseError);
}

TEST(Expression, EmptyExpression) {
    // Empty expression after brace stripping should throw
    // Note: depends on how empty is handled — just verify no crash / throws
    EXPECT_THROW(eval("{}"), ParseError);
}

TEST(Expression, UnknownFunction) {
    EXPECT_THROW(eval("foo(1)"), ParseError);
}

TEST(Expression, WrongArgCount) {
    EXPECT_THROW(eval("sqrt(1, 2)"), ParseError);
    EXPECT_THROW(eval("min(1)"), ParseError);
    EXPECT_THROW(eval("if(1, 2)"), ParseError);
}

// ============================================================
// .func expansion (expand_funcs)
// ============================================================

TEST(ExpandFuncs, BasicExpansion) {
    std::unordered_map<std::string, FuncDef> fdefs;
    fdefs["square"] = FuncDef{{"x"}, "x*x"};
    EXPECT_EQ(expand_funcs("square(3)", fdefs), "((3)*(3))");
    // The expanded result should evaluate to 9
    EXPECT_DOUBLE_EQ(eval(expand_funcs("square(3)", fdefs)), 9.0);
}

TEST(ExpandFuncs, TwoArgFunction) {
    std::unordered_map<std::string, FuncDef> fdefs;
    fdefs["myfunc"] = FuncDef{{"x", "y"}, "x*y+1"};
    std::string expanded = expand_funcs("myfunc(3, 4)", fdefs);
    EXPECT_DOUBLE_EQ(eval(expanded), 13.0);
}

TEST(ExpandFuncs, NestedFuncCalls) {
    std::unordered_map<std::string, FuncDef> fdefs;
    fdefs["square"] = FuncDef{{"x"}, "x*x"};
    fdefs["double"] = FuncDef{{"x"}, "2*x"};
    // double(square(3)) = double(9) = 18
    std::string expanded = expand_funcs("double(square(3))", fdefs);
    EXPECT_DOUBLE_EQ(eval(expanded), 18.0);
}

TEST(ExpandFuncs, CaseInsensitive) {
    std::unordered_map<std::string, FuncDef> fdefs;
    fdefs["square"] = FuncDef{{"x"}, "x*x"};
    // SQUARE should match square (case-insensitive)
    EXPECT_DOUBLE_EQ(eval(expand_funcs("SQUARE(5)", fdefs)), 25.0);
    EXPECT_DOUBLE_EQ(eval(expand_funcs("Square(4)", fdefs)), 16.0);
}

TEST(ExpandFuncs, NoExpansionWhenNoMatch) {
    std::unordered_map<std::string, FuncDef> fdefs;
    fdefs["square"] = FuncDef{{"x"}, "x*x"};
    // "squares" should not match "square"
    EXPECT_EQ(expand_funcs("squares(3)", fdefs), "squares(3)");
    // "asquare" should not match "square"
    EXPECT_EQ(expand_funcs("asquare(3)", fdefs), "asquare(3)");
}

TEST(ExpandFuncs, WordBoundaryInBody) {
    std::unordered_map<std::string, FuncDef> fdefs;
    // param name "a" should not match "abs" in body
    fdefs["myfunc"] = FuncDef{{"a"}, "abs(a)"};
    std::string expanded = expand_funcs("myfunc(5)", fdefs);
    // Should expand to (abs((5))) not ((5)bs((5)))
    EXPECT_DOUBLE_EQ(eval(expanded), 5.0);
}

TEST(ExpandFuncs, EmptyFuncDefs) {
    std::unordered_map<std::string, FuncDef> fdefs;
    EXPECT_EQ(expand_funcs("1+2", fdefs), "1+2");
}

TEST(ExpandFuncs, ExpressionWithBraces) {
    std::unordered_map<std::string, FuncDef> fdefs;
    fdefs["gain"] = FuncDef{{"x"}, "2*x"};
    // Test with braces around expression
    EXPECT_DOUBLE_EQ(eval(expand_funcs("{gain(5)}", fdefs)), 10.0);
}

TEST(ExpandFuncs, ArgPrecedence) {
    // Make sure parenthesization of args prevents precedence bugs
    std::unordered_map<std::string, FuncDef> fdefs;
    fdefs["square"] = FuncDef{{"x"}, "x*x"};
    // square(1+2) should be (1+2)*(1+2) = 9, not 1+2*1+2 = 5
    EXPECT_DOUBLE_EQ(eval(expand_funcs("square(1+2)", fdefs)), 9.0);
}

TEST(ExpandFuncs, WrongArgCountSkipped) {
    std::unordered_map<std::string, FuncDef> fdefs;
    fdefs["myfunc"] = FuncDef{{"x", "y"}, "x+y"};
    // Wrong number of args — should not expand
    std::string result = expand_funcs("myfunc(3)", fdefs);
    EXPECT_EQ(result, "myfunc(3)");
}
