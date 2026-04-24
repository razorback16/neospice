#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "devices/asrc/expression_ast.hpp"
#include <cmath>
#include <vector>

using namespace neospice;
using namespace neospice::asrc;

// ===========================================================================
// Expression AST unit tests
// ===========================================================================

TEST(ASRCExpr, ConstantExpr) {
    auto expr = CompiledExpression::compile("42.0");
    EXPECT_EQ(expr.num_vars(), 0);
    std::vector<double> vals, derivs;
    double result = expr.evaluate(vals, derivs);
    EXPECT_DOUBLE_EQ(result, 42.0);
    EXPECT_TRUE(derivs.empty());
}

TEST(ASRCExpr, SimpleArithmetic) {
    auto expr = CompiledExpression::compile("2 + 3 * 4");
    std::vector<double> vals, derivs;
    double result = expr.evaluate(vals, derivs);
    EXPECT_DOUBLE_EQ(result, 14.0);
}

TEST(ASRCExpr, PowerOperator) {
    auto expr = CompiledExpression::compile("2 ** 3");
    std::vector<double> vals, derivs;
    double result = expr.evaluate(vals, derivs);
    EXPECT_DOUBLE_EQ(result, 8.0);
}

TEST(ASRCExpr, CaretPower) {
    auto expr = CompiledExpression::compile("3 ^ 2");
    std::vector<double> vals, derivs;
    double result = expr.evaluate(vals, derivs);
    EXPECT_DOUBLE_EQ(result, 9.0);
}

TEST(ASRCExpr, NodeVoltageRef) {
    auto expr = CompiledExpression::compile("V(in) * 2");
    EXPECT_EQ(expr.num_vars(), 1);
    EXPECT_EQ(expr.var_refs()[0].kind, VarKind::NODE_VOLTAGE);
    EXPECT_EQ(expr.var_refs()[0].name1, "in");

    std::vector<double> vals = {3.0};
    std::vector<double> derivs;
    double result = expr.evaluate(vals, derivs);
    EXPECT_DOUBLE_EQ(result, 6.0);
    ASSERT_EQ(derivs.size(), 1u);
    EXPECT_DOUBLE_EQ(derivs[0], 2.0);
}

TEST(ASRCExpr, DiffVoltageRef) {
    auto expr = CompiledExpression::compile("V(a, b) + 1");
    EXPECT_EQ(expr.num_vars(), 1);
    EXPECT_EQ(expr.var_refs()[0].kind, VarKind::DIFF_VOLTAGE);
    EXPECT_EQ(expr.var_refs()[0].name1, "a");
    EXPECT_EQ(expr.var_refs()[0].name2, "b");

    std::vector<double> vals = {5.0};
    std::vector<double> derivs;
    double result = expr.evaluate(vals, derivs);
    EXPECT_DOUBLE_EQ(result, 6.0);
    EXPECT_DOUBLE_EQ(derivs[0], 1.0);
}

TEST(ASRCExpr, BranchCurrentRef) {
    auto expr = CompiledExpression::compile("I(V1) * 1000");
    EXPECT_EQ(expr.num_vars(), 1);
    EXPECT_EQ(expr.var_refs()[0].kind, VarKind::BRANCH_CURRENT);
    EXPECT_EQ(expr.var_refs()[0].name1, "v1");

    std::vector<double> vals = {0.001};
    std::vector<double> derivs;
    double result = expr.evaluate(vals, derivs);
    EXPECT_DOUBLE_EQ(result, 1.0);
    EXPECT_DOUBLE_EQ(derivs[0], 1000.0);
}

TEST(ASRCExpr, SinDerivative) {
    auto expr = CompiledExpression::compile("sin(V(x))");
    std::vector<double> vals = {M_PI / 6.0};
    std::vector<double> derivs;
    double result = expr.evaluate(vals, derivs);
    EXPECT_NEAR(result, 0.5, 1e-12);
    EXPECT_NEAR(derivs[0], std::cos(M_PI / 6.0), 1e-12);
}

TEST(ASRCExpr, ExpDerivative) {
    auto expr = CompiledExpression::compile("exp(V(x))");
    std::vector<double> vals = {1.0};
    std::vector<double> derivs;
    double result = expr.evaluate(vals, derivs);
    EXPECT_NEAR(result, M_E, 1e-12);
    EXPECT_NEAR(derivs[0], M_E, 1e-12);
}

TEST(ASRCExpr, ProductRule) {
    auto expr = CompiledExpression::compile("V(x) * V(y)");
    EXPECT_EQ(expr.num_vars(), 2);
    std::vector<double> vals = {3.0, 4.0};
    std::vector<double> derivs;
    double result = expr.evaluate(vals, derivs);
    EXPECT_DOUBLE_EQ(result, 12.0);
    EXPECT_DOUBLE_EQ(derivs[0], 4.0);
    EXPECT_DOUBLE_EQ(derivs[1], 3.0);
}

TEST(ASRCExpr, ChainRule) {
    auto expr = CompiledExpression::compile("sin(V(x) * 2)");
    double x = 0.5;
    std::vector<double> vals = {x};
    std::vector<double> derivs;
    double result = expr.evaluate(vals, derivs);
    EXPECT_NEAR(result, std::sin(2 * x), 1e-12);
    EXPECT_NEAR(derivs[0], 2.0 * std::cos(2 * x), 1e-12);
}

TEST(ASRCExpr, AbsDerivative) {
    auto expr = CompiledExpression::compile("abs(V(x))");
    {
        std::vector<double> vals = {3.0};
        std::vector<double> derivs;
        double result = expr.evaluate(vals, derivs);
        EXPECT_DOUBLE_EQ(result, 3.0);
        EXPECT_DOUBLE_EQ(derivs[0], 1.0);
    }
    {
        std::vector<double> vals = {-5.0};
        std::vector<double> derivs;
        double result = expr.evaluate(vals, derivs);
        EXPECT_DOUBLE_EQ(result, 5.0);
        EXPECT_DOUBLE_EQ(derivs[0], -1.0);
    }
}

TEST(ASRCExpr, MinMaxDerivative) {
    auto expr_min = CompiledExpression::compile("min(V(x), V(y))");
    {
        std::vector<double> vals = {2.0, 5.0};
        std::vector<double> derivs;
        double result = expr_min.evaluate(vals, derivs);
        EXPECT_DOUBLE_EQ(result, 2.0);
        EXPECT_DOUBLE_EQ(derivs[0], 1.0);
        EXPECT_DOUBLE_EQ(derivs[1], 0.0);
    }
    auto expr_max = CompiledExpression::compile("max(V(x), V(y))");
    {
        std::vector<double> vals = {2.0, 5.0};
        std::vector<double> derivs;
        double result = expr_max.evaluate(vals, derivs);
        EXPECT_DOUBLE_EQ(result, 5.0);
        EXPECT_DOUBLE_EQ(derivs[0], 0.0);
        EXPECT_DOUBLE_EQ(derivs[1], 1.0);
    }
}

TEST(ASRCExpr, IfFunction) {
    auto expr = CompiledExpression::compile("if(V(x), V(y), V(z))");
    // Positive condition -> then branch
    {
        std::vector<double> vals = {1.0, 10.0, 20.0};
        std::vector<double> derivs;
        double result = expr.evaluate(vals, derivs);
        EXPECT_DOUBLE_EQ(result, 10.0);
        EXPECT_DOUBLE_EQ(derivs[1], 1.0);
        EXPECT_DOUBLE_EQ(derivs[2], 0.0);
    }
    // Negative condition (non-zero) -> then branch (ngspice: any non-zero is true)
    {
        std::vector<double> vals = {-1.0, 10.0, 20.0};
        std::vector<double> derivs;
        double result = expr.evaluate(vals, derivs);
        EXPECT_DOUBLE_EQ(result, 10.0);
        EXPECT_DOUBLE_EQ(derivs[1], 1.0);
        EXPECT_DOUBLE_EQ(derivs[2], 0.0);
    }
    // Zero condition -> else branch
    {
        std::vector<double> vals = {0.0, 10.0, 20.0};
        std::vector<double> derivs;
        double result = expr.evaluate(vals, derivs);
        EXPECT_DOUBLE_EQ(result, 20.0);
        EXPECT_DOUBLE_EQ(derivs[1], 0.0);
        EXPECT_DOUBLE_EQ(derivs[2], 1.0);
    }
}

TEST(ASRCExpr, SpiceSuffixes) {
    auto expr = CompiledExpression::compile("1k + 2.5m");
    std::vector<double> vals, derivs;
    double result = expr.evaluate(vals, derivs);
    EXPECT_NEAR(result, 1000.0 + 0.0025, 1e-9);
}

TEST(ASRCExpr, NestedFunctions) {
    auto expr = CompiledExpression::compile("sqrt(V(x) * V(x) + V(y) * V(y))");
    double x = 3.0, y = 4.0;
    std::vector<double> vals = {x, y};
    std::vector<double> derivs;
    double result = expr.evaluate(vals, derivs);
    EXPECT_NEAR(result, 5.0, 1e-12);
    EXPECT_NEAR(derivs[0], 0.6, 1e-12);
    EXPECT_NEAR(derivs[1], 0.8, 1e-12);
}

TEST(ASRCExpr, DuplicateVarRefs) {
    auto expr = CompiledExpression::compile("V(x) * V(x)");
    EXPECT_EQ(expr.num_vars(), 1);
    std::vector<double> vals = {3.0};
    std::vector<double> derivs;
    double result = expr.evaluate(vals, derivs);
    EXPECT_DOUBLE_EQ(result, 9.0);
    EXPECT_DOUBLE_EQ(derivs[0], 6.0);
}

TEST(ASRCExpr, LimitFunction) {
    auto expr = CompiledExpression::compile("limit(V(x), 0, 5)");
    {
        std::vector<double> vals = {3.0};
        std::vector<double> derivs;
        EXPECT_DOUBLE_EQ(expr.evaluate(vals, derivs), 3.0);
        EXPECT_DOUBLE_EQ(derivs[0], 1.0);
    }
    {
        std::vector<double> vals = {-2.0};
        std::vector<double> derivs;
        EXPECT_DOUBLE_EQ(expr.evaluate(vals, derivs), 0.0);
        EXPECT_DOUBLE_EQ(derivs[0], 0.0);
    }
    {
        std::vector<double> vals = {10.0};
        std::vector<double> derivs;
        EXPECT_DOUBLE_EQ(expr.evaluate(vals, derivs), 5.0);
        EXPECT_DOUBLE_EQ(derivs[0], 0.0);
    }
}

TEST(ASRCExpr, TrigFunctions) {
    // cos
    {
        auto expr = CompiledExpression::compile("cos(V(x))");
        std::vector<double> vals = {0.0};
        std::vector<double> derivs;
        EXPECT_NEAR(expr.evaluate(vals, derivs), 1.0, 1e-12);
        EXPECT_NEAR(derivs[0], 0.0, 1e-12);
    }
    // tan
    {
        auto expr = CompiledExpression::compile("tan(V(x))");
        std::vector<double> vals = {M_PI / 4.0};
        std::vector<double> derivs;
        EXPECT_NEAR(expr.evaluate(vals, derivs), 1.0, 1e-12);
        EXPECT_NEAR(derivs[0], 2.0, 1e-12);
    }
}

TEST(ASRCExpr, HyperbolicFunctions) {
    auto expr = CompiledExpression::compile("tanh(V(x))");
    std::vector<double> vals = {0.0};
    std::vector<double> derivs;
    EXPECT_NEAR(expr.evaluate(vals, derivs), 0.0, 1e-12);
    EXPECT_NEAR(derivs[0], 1.0, 1e-12);  // sech^2(0) = 1
}

TEST(ASRCExpr, DivisionDerivative) {
    // f(x,y) = x/y => df/dx = 1/y, df/dy = -x/y^2
    auto expr = CompiledExpression::compile("V(x) / V(y)");
    std::vector<double> vals = {6.0, 3.0};
    std::vector<double> derivs;
    double result = expr.evaluate(vals, derivs);
    EXPECT_DOUBLE_EQ(result, 2.0);
    EXPECT_NEAR(derivs[0], 1.0 / 3.0, 1e-12);
    EXPECT_NEAR(derivs[1], -6.0 / 9.0, 1e-12);
}

TEST(ASRCExpr, UnaryMinus) {
    auto expr = CompiledExpression::compile("-V(x)");
    std::vector<double> vals = {5.0};
    std::vector<double> derivs;
    EXPECT_DOUBLE_EQ(expr.evaluate(vals, derivs), -5.0);
    EXPECT_DOUBLE_EQ(derivs[0], -1.0);
}

TEST(ASRCExpr, LogDerivative) {
    auto expr = CompiledExpression::compile("log(V(x))");
    std::vector<double> vals = {M_E};
    std::vector<double> derivs;
    EXPECT_NEAR(expr.evaluate(vals, derivs), 1.0, 1e-12);
    EXPECT_NEAR(derivs[0], 1.0 / M_E, 1e-12);
}

TEST(ASRCExpr, PowFunction) {
    auto expr = CompiledExpression::compile("pow(V(x), 3)");
    std::vector<double> vals = {2.0};
    std::vector<double> derivs;
    EXPECT_DOUBLE_EQ(expr.evaluate(vals, derivs), 8.0);
    EXPECT_NEAR(derivs[0], 12.0, 1e-12);  // 3*x^2 = 3*4 = 12
}

// ===========================================================================
// ASRC device integration tests via API
// ===========================================================================

TEST(ASRC, VoltageDoubler) {
    // B1 out 0 V={V(in)*2}  with V(in) = 1V
    // Expected: V(out) = 2V
    Simulator sim;
    auto ckt = sim.parse(R"(
Voltage Doubler
V1 in 0 DC 1.0
R1 out 0 1k
B1 out 0 V={V(in)*2}
.op
.end
)");
    auto dc = sim.run_dc(ckt);
    EXPECT_NEAR(dc.voltage("out"), 2.0, 1e-6);
}

TEST(ASRC, BehavioralVCCS) {
    // B1 0 out I={V(in)*1m}  with V(in)=5V, R=1k
    // Current = 5*1m = 5mA flowing from 0 to out => V(out) = I*R = 5V
    Simulator sim;
    auto ckt = sim.parse(R"(
VCCS via B element
V1 in 0 DC 5.0
R1 out 0 1k
B1 0 out I={V(in)*1m}
.op
.end
)");
    auto dc = sim.run_dc(ckt);
    EXPECT_NEAR(dc.voltage("out"), 5.0, 1e-6);
}

TEST(ASRC, NonlinearVoltage) {
    // B1 out 0 V={V(in)*V(in)}  with V(in)=3V
    // Expected: V(out) = 9V
    Simulator sim;
    auto ckt = sim.parse(R"(
Nonlinear B source
V1 in 0 DC 3.0
R1 out 0 1k
B1 out 0 V={V(in)*V(in)}
.op
.end
)");
    auto dc = sim.run_dc(ckt);
    EXPECT_NEAR(dc.voltage("out"), 9.0, 1e-6);
}

TEST(ASRC, SinFunction) {
    // B1 out 0 V={sin(V(in))}  with V(in) = pi/6
    // Expected: V(out) = 0.5
    Simulator sim;
    auto ckt = sim.parse(R"(
Sine B source
V1 in 0 DC 0.5235987755983
R1 out 0 1k
B1 out 0 V={sin(V(in))}
.op
.end
)");
    auto dc = sim.run_dc(ckt);
    EXPECT_NEAR(dc.voltage("out"), 0.5, 1e-6);
}

TEST(ASRC, ConstantVoltage) {
    // B1 out 0 V={5.0}
    Simulator sim;
    auto ckt = sim.parse(R"(
Constant B source
R1 out 0 1k
B1 out 0 V={5.0}
.op
.end
)");
    auto dc = sim.run_dc(ckt);
    EXPECT_NEAR(dc.voltage("out"), 5.0, 1e-6);
}

TEST(ASRC, DiffVoltage) {
    // B1 out 0 V={V(a,b)} where V(a)=5, V(b)=2
    // Expected: V(out) = 3
    Simulator sim;
    auto ckt = sim.parse(R"(
Differential voltage B source
V1 a 0 DC 5.0
V2 b 0 DC 2.0
R1 out 0 1k
B1 out 0 V={V(a,b)}
.op
.end
)");
    auto dc = sim.run_dc(ckt);
    EXPECT_NEAR(dc.voltage("out"), 3.0, 1e-6);
}

TEST(ASRC, ExpFunction) {
    // B1 out 0 V={exp(V(in))}  with V(in)=1
    // Expected: V(out) = e ≈ 2.71828...
    Simulator sim;
    auto ckt = sim.parse(R"(
Exp B source
V1 in 0 DC 1.0
R1 out 0 1k
B1 out 0 V={exp(V(in))}
.op
.end
)");
    auto dc = sim.run_dc(ckt);
    EXPECT_NEAR(dc.voltage("out"), M_E, 1e-4);
}

TEST(ASRC, MultipleVariables) {
    // B1 out 0 V={V(a) + V(b) * 2}
    // V(a)=1, V(b)=3 => V(out) = 1 + 6 = 7
    Simulator sim;
    auto ckt = sim.parse(R"(
Multi-variable B source
V1 a 0 DC 1.0
V2 b 0 DC 3.0
R1 out 0 1k
B1 out 0 V={V(a)+V(b)*2}
.op
.end
)");
    auto dc = sim.run_dc(ckt);
    EXPECT_NEAR(dc.voltage("out"), 7.0, 1e-6);
}

TEST(ASRC, CubeFunction) {
    // B1 out 0 V={V(in) * V(in) * V(in)}  with V(in) = 2
    // Expected: V(out) = 8
    Simulator sim;
    auto ckt = sim.parse(R"(
Cube B source
V1 in 0 DC 2.0
R1 out 0 1k
B1 out 0 V={V(in)*V(in)*V(in)}
.op
.end
)");
    auto dc = sim.run_dc(ckt);
    EXPECT_NEAR(dc.voltage("out"), 8.0, 1e-6);
}

TEST(ASRC, AbsFunction) {
    // B1 out 0 V={abs(V(in))}  with V(in) = -3
    // Expected: V(out) = 3
    Simulator sim;
    auto ckt = sim.parse(R"(
Abs B source
V1 in 0 DC -3.0
R1 out 0 1k
B1 out 0 V={abs(V(in))}
.op
.end
)");
    auto dc = sim.run_dc(ckt);
    EXPECT_NEAR(dc.voltage("out"), 3.0, 1e-6);
}

// ===========================================================================
// IDT() function tests
// ===========================================================================

TEST(ASRCExpr, IdtParseNoIC) {
    // IDT(V(in)) should parse without error
    auto expr = CompiledExpression::compile("IDT(V(in))");
    EXPECT_EQ(expr.num_vars(), 1);
    EXPECT_EQ(expr.var_refs()[0].kind, VarKind::NODE_VOLTAGE);
    EXPECT_EQ(expr.var_refs()[0].name1, "in");
}

TEST(ASRCExpr, IdtParseWithIC) {
    // IDT(V(in), 5.0) should parse with IC = 5.0
    auto expr = CompiledExpression::compile("IDT(V(in), 5.0)");
    EXPECT_EQ(expr.num_vars(), 1);
}

TEST(ASRCExpr, IdtParseWithICAndAssert) {
    // IDT(V(in), 1.0, 10.0) — third arg (assert) parsed and discarded
    auto expr = CompiledExpression::compile("IDT(V(in), 1.0, 10.0)");
    EXPECT_EQ(expr.num_vars(), 1);
}

TEST(ASRC, IdtConstantInput) {
    // IDT(1) over 0.5s should yield V(out) ~ t (linear ramp)
    Simulator sim;
    auto ckt = sim.parse(R"(
IDT constant test
V1 in 0 1
R1 in 0 1k
B1 out 0 V={IDT(V(in))}
R2 out 0 1Meg
.tran 0.01 0.5
.end
)");
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(result.analysis));
    const auto& tr = std::get<TransientResult>(result.analysis);
    const auto& t = tr.time;
    const auto& v_out = tr.voltage("out");
    // At the last timepoint (t ~ 0.5), V(out) should be ~ 0.5
    double t_last = t.back();
    double v_last = v_out.back();
    EXPECT_NEAR(v_last, t_last, 1e-3)
        << "IDT(1) at t=" << t_last << ": expected " << t_last << " got " << v_last;
}

TEST(ASRC, IdtWithInitialCondition) {
    // IDT(1, 10.0) should start at 10.0 and ramp: V(out) ~ 10 + t
    Simulator sim;
    auto ckt = sim.parse(R"(
IDT IC test
V1 in 0 1
R1 in 0 1k
B1 out 0 V={IDT(V(in), 10.0)}
R2 out 0 1Meg
.tran 0.01 0.5
.end
)");
    auto result = sim.run(ckt);
    ASSERT_TRUE(std::holds_alternative<TransientResult>(result.analysis));
    const auto& tr = std::get<TransientResult>(result.analysis);
    const auto& t = tr.time;
    const auto& v_out = tr.voltage("out");
    // At the last timepoint (t ~ 0.5), V(out) should be ~ 10.5
    double t_last = t.back();
    double v_last = v_out.back();
    EXPECT_NEAR(v_last, 10.0 + t_last, 1e-3)
        << "IDT(1, 10) at t=" << t_last << ": expected " << (10.0 + t_last) << " got " << v_last;
}

// ===========================================================================
// .func user-defined function support
// ===========================================================================

TEST(ASRC, FuncInBehavioralSource) {
    // .func gain(x) {2*x}
    // B1 out 0 V={gain(V(in))}
    // Expected: V(out) = 2 * V(in) = 2.0
    Simulator sim;
    auto ckt = sim.parse(R"(
Func gain test
.func gain(x) {2*x}
V1 in 0 DC 1.0
R1 out 0 1k
B1 out 0 V={gain(V(in))}
.op
.end
)");
    auto dc = sim.run_dc(ckt);
    EXPECT_NEAR(dc.voltage("out"), 2.0, 1e-6);
}

TEST(ASRC, FuncSquareInBehavioralSource) {
    // .func square(a) {a*a}
    // B1 out 0 V={square(V(in))}  with V(in) = 3V
    // Expected: V(out) = 9.0
    Simulator sim;
    auto ckt = sim.parse(R"(
Func square in ASRC
.func square(a) {a*a}
V1 in 0 DC 3.0
R1 out 0 1k
B1 out 0 V={square(V(in))}
.op
.end
)");
    auto dc = sim.run_dc(ckt);
    EXPECT_NEAR(dc.voltage("out"), 9.0, 1e-6);
}

TEST(ASRC, FuncTwoArgsInBehavioralSource) {
    // .func myfunc(x, y) {x*y+1}
    // B1 out 0 V={myfunc(V(in1), V(in2))}  with V(in1)=3, V(in2)=4
    // Expected: V(out) = 3*4+1 = 13.0
    Simulator sim;
    auto ckt = sim.parse(R"(
Func two args in ASRC
.func myfunc(x, y) {x*y+1}
V1 in1 0 DC 3.0
V2 in2 0 DC 4.0
R1 out 0 1k
B1 out 0 V={myfunc(V(in1), V(in2))}
.op
.end
)");
    auto dc = sim.run_dc(ckt);
    EXPECT_NEAR(dc.voltage("out"), 13.0, 1e-6);
}

TEST(ASRC, NestedFuncCallsInASRC) {
    // .func square(x) {x*x}
    // .func mydouble(x) {2*x}
    // B1 out 0 V={mydouble(square(V(in)))}  with V(in) = 3V
    // Expected: V(out) = 2*(3*3) = 18.0
    Simulator sim;
    auto ckt = sim.parse(R"(
Nested func calls in ASRC
.func square(x) {x*x}
.func mydouble(x) {2*x}
V1 in 0 DC 3.0
R1 out 0 1k
B1 out 0 V={mydouble(square(V(in)))}
.op
.end
)");
    auto dc = sim.run_dc(ckt);
    EXPECT_NEAR(dc.voltage("out"), 18.0, 1e-6);
}

TEST(ASRC, FuncComposedInExpr) {
    // .func square(x) {x*x}
    // B1 out 0 V={square(V(in))+1}  with V(in) = 4V
    // Expected: V(out) = 16+1 = 17
    Simulator sim;
    auto ckt = sim.parse(R"(
Func composed in expression
.func square(x) {x*x}
V1 in 0 DC 4.0
R1 out 0 1k
B1 out 0 V={square(V(in))+1}
.op
.end
)");
    auto dc = sim.run_dc(ckt);
    EXPECT_NEAR(dc.voltage("out"), 17.0, 1e-6);
}
