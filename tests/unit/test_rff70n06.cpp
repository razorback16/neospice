#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "framework/ngspice_runner.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>

using namespace neospice;

TEST(RFF70N06, OriginalCorpusOperatingPoint) {
    // Dedicated executable: compatibility mode is process-global. This is
    // the original corpus's ngspice -D ngbehavior=psa configuration.
    NgspiceLib configuration;
    configuration.command("set ngbehavior=psa");
    NgspiceRunner reference_runner;
    const std::string path = TEST_RFF70N06_CIRCUIT;
    const auto reference = reference_runner.run_dc(path);
    ASSERT_TRUE(reference.status.converged);
    for (const std::string node : {"net_1", "net_2", "net_3"}) {
        const std::string name = "v(" + node + ")";
        ASSERT_TRUE(reference.node_voltages.contains(name));
        const double value = reference.node_voltages.at(name);
        ASSERT_TRUE(std::isfinite(value));
        std::cerr << std::setprecision(17) << "DETAIL_RFF_REFERENCE|"
                  << name << '|' << value << '\n';
    }

    Simulator simulator;
    auto circuit = simulator.load(path);
    circuit.options.no_throw = true;
    const auto actual = simulator.run_dc(circuit);
    ASSERT_TRUE(actual.status.converged)
        << "iterations=" << actual.status.iterations
        << " residual=" << actual.status.residual;

    // Require the original corpus's three external observables. This test's
    // reference-magnitude allowance is stricter than the corpus's symmetric one.
    for (const std::string node : {"net_1", "net_2", "net_3"}) {
        const std::string name = "v(" + node + ")";
        ASSERT_TRUE(reference.node_voltages.contains(name));
        ASSERT_TRUE(actual.node_voltages.contains(name));
        const double expected = reference.node_voltages.at(name);
        const double observed = actual.node_voltages.at(name);
        ASSERT_TRUE(std::isfinite(expected));
        ASSERT_TRUE(std::isfinite(observed));
        const double allowance = 1e-3 * std::abs(expected) + 1e-6;
        std::cerr << std::setprecision(17) << "DETAIL_RFF_OP|" << name << '|'
                  << expected << '|' << observed << '|'
                  << std::abs(observed - expected) << '|' << allowance << '\n';
        EXPECT_LE(std::abs(observed - expected), allowance) << name;
    }
}
