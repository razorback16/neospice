#include "api/neospice.hpp"
#include "framework/ngspice_runner.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numeric>

using namespace neospice;
namespace {
const std::string divider = "Divider\n.param r=1k other=2k derived={2*r}\nV1 in 0 6\nR1 in out {derived}\nR2 out 0 {other}\n.op\n.end\n";
double voltage(const SweepSample& sample) {
    if (!sample.error.empty() || !sample.result) throw std::runtime_error(sample.error);
    return std::get<DCResult>(sample.result->analysis).voltage("out");
}
}

TEST(ParallelSweep, OrderedIndependentOverrides) {
    std::vector<SweepPoint> points(40);
    for (std::size_t i = 0; i < points.size(); ++i) points[i].parameters["R"] = 1000 + 100 * i;
    Simulator sim;
    auto serial = sim.run_sweep(divider, points, {.workers=1});
    auto parallel = sim.run_sweep(divider, points, {.workers=4});
    ASSERT_EQ(parallel.samples.size(), points.size());
    EXPECT_EQ(parallel.workers_used, 4);
    for (std::size_t i = 0; i < points.size(); ++i) {
        ASSERT_TRUE(parallel.samples[i].error.empty()) << parallel.samples[i].error;
        EXPECT_DOUBLE_EQ(voltage(serial.samples[i]), voltage(parallel.samples[i]));
        EXPECT_NEAR(voltage(parallel.samples[i]), 6 * 2000 / (4000.0 + 200 * i), 1e-10);
    }
}

TEST(ParallelSweep, FailedJobsRetainOrderAndDoNotCancelOthers) {
    std::vector<SweepPoint> points(3);
    points[1].parameters["typo"] = 1;
    auto result = Simulator().run_sweep(divider, points, {.workers=3});
    EXPECT_NEAR(voltage(result.samples[0]), 3, 1e-12);
    EXPECT_FALSE(result.samples[1].error.empty());
    EXPECT_FALSE(result.samples[1].result);
    EXPECT_NEAR(voltage(result.samples[2]), 3, 1e-12);
    auto empty = Simulator().run_sweep(divider, {});
    EXPECT_EQ(empty.workers_used, 0);
    EXPECT_TRUE(empty.samples.empty());
}

TEST(ParallelSweep, SubcircuitDefaultsAndLocalParameters) {
    const std::string text = "Subcircuit\n.param r=1k derived={r*2}\n.subckt pair in out local=3k\nR1 in out {derived}\nR2 out 0 {local}\n.ends\nX1 in out pair\nV1 in 0 10\n.op\n.end\n";
    SweepPoint point; point.parameters["r"] = 2000;
    auto result = Simulator().run_sweep(text, {point});
    EXPECT_NEAR(voltage(result.samples[0]), 30.0/7, 1e-10);
}

TEST(ParallelSweep, DeviceAndTemperatureCorners) {
    SweepPoint point; point.device_values["r2"] = 4000; point.temperature_celsius = 75;
    auto result = Simulator().run_sweep(divider, {point}, {.workers=8});
    EXPECT_EQ(result.workers_used, 1);
    EXPECT_NEAR(voltage(result.samples[0]), 4, 1e-12);
    point.temperature_celsius = -274;
    EXPECT_FALSE(Simulator().run_sweep(divider, {point}).samples[0].error.empty());
    point.temperature_celsius = 27; point.device_values["bad"] = 5;
    EXPECT_FALSE(Simulator().run_sweep(divider, {point}).samples[0].error.empty());
}

TEST(ParallelSweep, RejectsAmbiguousAnalysesAndNestedStep) {
    for (auto text : {"Bad\nV1 in 0 1\nR1 in 0 1k\n.end\n",
                      "Bad\nV1 in 0 1\nR1 in 0 1k\n.op\n.ac lin 1 1 1\n.end\n",
                      "Bad\n.param r=1k\nV1 in 0 1\nR1 in 0 {r}\n.op\n.step param r 1k 2k 1k\n.end\n"}) {
        EXPECT_FALSE(Simulator().run_sweep(text, {SweepPoint{}}).samples[0].error.empty());
    }
}

TEST(ParallelSweep, SeededExpressionFunctions) {
    std::string text = "Random\n.param r={agauss(1000,20,1)}\nV1 in 0 6\nR1 in out {r}\nR2 out 0 1k\n.op\n.end\n";
    std::vector<SweepPoint> points(32);
    auto a = Simulator().run_sweep(text, points, {.workers=1,.seed=123});
    auto b = Simulator().run_sweep(text, points, {.workers=4,.seed=123});
    for (std::size_t i = 0; i < points.size(); ++i) EXPECT_DOUBLE_EQ(voltage(a.samples[i]), voltage(b.samples[i]));
    EXPECT_NE(voltage(a.samples[0]), voltage(a.samples[1]));
}

TEST(ParallelSweep, ConcurrentMigratedModelSetup) {
    const std::string text = "Diode\n.param supply=1\nV1 in 0 {supply}\nR1 in out 1k\nD1 out 0 dm\n.model dm D(IS=1e-14 RS=10)\n.op\n.end\n";
    std::vector<SweepPoint> points(64);
    for (std::size_t i=0; i<points.size(); ++i) points[i].parameters["supply"] = 1 + i*0.01;
    auto a = Simulator().run_sweep(text, points, {.workers=1});
    auto b = Simulator().run_sweep(text, points, {.workers=8});
    for (std::size_t i=0; i<points.size(); ++i) EXPECT_DOUBLE_EQ(voltage(a.samples[i]), voltage(b.samples[i]));
}

TEST(MonteCarlo, RepeatableAcrossWorkerCounts) {
    std::vector<ParameterVariation> params{{"r",1000,100},{"other",2000,200,VariationDistribution::Uniform}};
    MonteCarloOptions options; options.samples=32; options.execution={.workers=1,.seed=987};
    auto a = Simulator().monte_carlo(divider, params, options);
    options.execution.workers=4;
    auto b = Simulator().monte_carlo(divider, params, options);
    for (std::size_t i=0;i<a.samples.size();++i) {
        EXPECT_EQ(a.samples[i].point.parameters, b.samples[i].point.parameters);
        EXPECT_DOUBLE_EQ(voltage(a.samples[i]),voltage(b.samples[i]));
        EXPECT_GE(a.samples[i].point.parameters.at("other"),1800);
        EXPECT_LE(a.samples[i].point.parameters.at("other"),2200);
    }
    options.execution.seed++;
    auto c = Simulator().monte_carlo(divider, params, options);
    EXPECT_NE(a.samples[0].point.parameters, c.samples[0].point.parameters);
}

TEST(MonteCarlo, CorrelationAndInputValidation) {
    MonteCarloOptions options; options.samples=30; options.execution.workers=3;
    options.correlation={{1,-1},{-1,1}};
    std::vector<ParameterVariation> params{{"r",1000,10},{"other",2000,20}};
    auto a=Simulator().monte_carlo(divider,params,options);
    for (auto& sample:a.samples) {
        EXPECT_NEAR((sample.point.parameters.at("r")-1000)/10,
                    -(sample.point.parameters.at("other")-2000)/20,1e-12);
    }
    for (auto matrix : std::vector<std::vector<std::vector<double>>>{{{1}},{{1,2},{2,1}},{{1,0.5},{0,1}},{{1,0},{0,0.5}}}) {
        options.correlation=matrix;
        EXPECT_THROW(Simulator().monte_carlo(divider,params,options),std::invalid_argument);
    }
    options.correlation={}; params[0].spread=-1;
    EXPECT_THROW(Simulator().monte_carlo(divider,params,options),std::invalid_argument);
    params[0].spread=1;params[1].parameter="R";
    EXPECT_THROW(Simulator().monte_carlo(divider,params,options),std::invalid_argument);
}

TEST(MonteCarlo, Statistics) {
    auto s=summarize_samples({1,2,3,4},2,3,2);
    EXPECT_EQ(s.count,4);
    EXPECT_DOUBLE_EQ(s.mean,2.5);
    EXPECT_NEAR(s.standard_deviation,std::sqrt(5.0/3),1e-14);
    EXPECT_DOUBLE_EQ(s.yield,0.5);
    EXPECT_EQ(s.histogram,(std::vector<std::size_t>{2,2}));
    auto c=summarize_samples({2,2},0,3,3);
    EXPECT_EQ(c.histogram[0],2);
    EXPECT_DOUBLE_EQ(c.standard_deviation,0);
    EXPECT_THROW(summarize_samples({},0,1),std::invalid_argument);
    EXPECT_THROW(summarize_samples({NAN},0,1),std::invalid_argument);
}

TEST(ParallelSweep, MatchesNgspice47ForDiodeCorners) {
    std::vector<SweepPoint> points(5);
    const std::string text = "Diode corners\n.param supply=1\nV1 in 0 {supply}\nR1 in out 1k\nD1 out 0 dm\n.model dm D(IS=1e-14 RS=10)\n.op\n.end\n";
    for (std::size_t i = 0; i < points.size(); ++i) points[i].parameters["supply"] = 0.5 + i*0.2;
    auto batch = Simulator().run_sweep(text, points, {.workers=4});
    const auto path = std::filesystem::temp_directory_path() / "neospice_parallel_sweep_reference.cir";
    NgspiceRunner reference;
    for (std::size_t i = 0; i < points.size(); ++i) {
        std::ofstream file(path);
        file << "Diode reference\nV1 in 0 " << 0.5+i*0.2
             << "\nR1 in out 1k\nD1 out 0 dm\n.model dm D(IS=1e-14 RS=10)\n.op\n.end\n";
        file.close();
        auto expected = reference.run_dc(path.string());
        EXPECT_NEAR(voltage(batch.samples[i]), expected.voltage("out"), 1e-7);
    }
    std::filesystem::remove(path);
}

TEST(ParallelSweep, ACAndTransientJobs) {
    const std::string body = "RC\n.param cap=1u\nV1 in 0 DC 1 AC 1 PULSE(0 1 0 1n 1n 1 2)\nR1 in out 1k\nC1 out 0 {cap}\n";
    std::vector<SweepPoint> points(8);
    for (std::size_t i=0; i<points.size(); ++i) points[i].parameters["cap"]=1e-6*(i+1);
    for (const auto& analysis : {".ac dec 5 10 1k\n.end\n", ".tran 10u 100u\n.end\n"}) {
        auto a=Simulator().run_sweep(body+analysis,points,{.workers=1});
        auto b=Simulator().run_sweep(body+analysis,points,{.workers=4});
        for(std::size_t i=0; i<points.size(); ++i) {
            ASSERT_TRUE(a.samples[i].error.empty()) << a.samples[i].error;
            ASSERT_TRUE(b.samples[i].error.empty()) << b.samples[i].error;
            if (auto* ac=std::get_if<ACResult>(&a.samples[i].result->analysis))
                EXPECT_EQ(ac->voltage("out"),std::get<ACResult>(b.samples[i].result->analysis).voltage("out"));
            else
                EXPECT_EQ(std::get<TransientResult>(a.samples[i].result->analysis).voltage("out"),
                          std::get<TransientResult>(b.samples[i].result->analysis).voltage("out"));
        }
    }
}

TEST(MonteCarlo, GaussianMomentsAndCorrelation) {
    MonteCarloOptions options; options.samples=4096; options.execution={.workers=4,.seed=2026};
    options.correlation={{1,0.6},{0.6,1}};
    auto batch=Simulator().monte_carlo(divider,{{"r",1000,10},{"other",2000,20}},options);
    double sx=0,sy=0,sxx=0,syy=0,sxy=0;
    for (const auto& sample:batch.samples) {
        ASSERT_TRUE(sample.error.empty()) << sample.error;
        double x=(sample.point.parameters.at("r")-1000)/10;
        double y=(sample.point.parameters.at("other")-2000)/20;
        sx+=x; sy+=y; sxx+=x*x; syy+=y*y; sxy+=x*y;
    }
    double n=options.samples;
    EXPECT_NEAR(sx/n,0,0.05); EXPECT_NEAR(sy/n,0,0.05);
    EXPECT_NEAR(sxx/n,1,0.08); EXPECT_NEAR(syy/n,1,0.08);
    EXPECT_NEAR((sxy-sx*sy/n)/std::sqrt((sxx-sx*sx/n)*(syy-sy*sy/n)),0.6,0.04);
    options.correlation={{1,0.9,0.9},{0.9,1,-0.9},{0.9,-0.9,1}};
    EXPECT_THROW(Simulator().monte_carlo(divider,{{"r",1,1},{"other",1,1},{"third",1,1}},options),std::invalid_argument);
}

TEST(ParallelSweep, ParameterizedDCAndACSources) {
    const std::string text="Sources\n.param bias=2 mag=3 phase=90\nV1 in 0 DC {bias} AC {mag} {phase}\nR1 in out 1k\nR2 out 0 1k\n.ac lin 1 100 100\n.end\n";
    SweepPoint point; point.parameters={{"bias",4},{"mag",8},{"phase",0}};
    auto batch=Simulator().run_sweep(text,{point});
    ASSERT_TRUE(batch.samples[0].error.empty()) << batch.samples[0].error;
    auto output=std::get<ACResult>(batch.samples[0].result->analysis).voltage("out");
    ASSERT_EQ(output.size(),1);
    EXPECT_NEAR(output[0].real(),4,1e-12);
    EXPECT_NEAR(output[0].imag(),0,1e-12);
}
