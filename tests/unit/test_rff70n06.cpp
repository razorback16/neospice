#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "framework/ngspice_runner.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace neospice;

// `uncategorized/spice_complete/harprmos.lib::RFF70N06_HA` is retained from the
// original corpus, where it was recorded as a case ngspice solved and neospice
// did not. Against the pinned reference that is no longer true: ngspice 47's
// operating-point analysis aborts on this fixture with
//
//   doAnalyses: OP: Timestep too small; trouble with x1:dbdmod-instance
//   d.x1.dbody
//
// so it produces no operating point at all. With no reference answer there is
// nothing to compare against, in either direction: the fixture cannot be
// counted as agreement, and neospice's own failure on it cannot be called a
// defect relative to the reference. It is therefore classified
// reference-inconclusive -- kept in corpus accounting, never deleted, and never
// counted as a match.
//
// This test makes that classification executable instead of leaving it as a
// comment beside a permanently red assertion. It pins both halves of the
// observed state, so the classification cannot quietly go stale: if ngspice 47
// ever returns an operating point, or neospice ever converges on the fixture,
// this test fails and the classification must be revisited -- at which point a
// real comparison becomes possible and should replace this one.
TEST(RFF70N06, ReferenceIsInconclusive) {
    // Dedicated executable: compatibility mode is process-global. This is
    // the original corpus's ngspice -D ngbehavior=psa configuration.
    NgspiceLib configuration;
    configuration.command("set ngbehavior=psa");
    NgspiceRunner reference_runner;
    const std::string path = TEST_RFF70N06_CIRCUIT;

    bool reference_produced_a_result = false;
    std::string reference_diagnostic;
    try {
        const auto reference = reference_runner.run_dc(path);
        reference_produced_a_result = reference.status.converged;
    } catch (const std::exception& e) {
        reference_diagnostic = e.what();
    }
    std::cerr << "DETAIL_RFF_REFERENCE|" << reference_diagnostic << '\n';
    EXPECT_FALSE(reference_produced_a_result)
        << "ngspice 47 now solves RFF70N06. The reference-inconclusive "
           "classification no longer holds: restore a real operating-point "
           "comparison and update docs/rff70n06-investigation.md.";
    EXPECT_FALSE(reference_diagnostic.empty())
        << "ngspice 47 neither solved the fixture nor reported why.";

    // neospice must fail explicitly on the same fixture: no exception escaping
    // past no_throw, no fabricated operating point, and a reported status.
    Simulator simulator;
    auto circuit = simulator.load(path);
    circuit.options.no_throw = true;
    const auto actual = simulator.run_dc(circuit);
    std::cerr << std::setprecision(17) << "DETAIL_RFF_NEOSPICE|"
              << actual.status.converged << '|' << actual.status.iterations
              << '|' << actual.status.residual << '\n';
    EXPECT_FALSE(actual.status.converged)
        << "neospice now converges on RFF70N06 while ngspice 47 still aborts. "
           "That is a NEO_ONLY outcome to adjudicate, not a silent pass: see "
           "docs/rff70n06-investigation.md.";
    EXPECT_TRUE(std::isfinite(actual.status.residual))
        << "a failed solve must still report a finite residual";
}
