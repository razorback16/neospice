#include <gtest/gtest.h>
#include "framework/ngspice_lib.hpp"

TEST(NgspiceLibrary, RejectsFrontendErrorsEvenWhenCommandReturnsZero) {
    NgspiceLib ng;
    EXPECT_THROW(ng.command("neospice_nonexistent_command"), std::runtime_error);
    EXPECT_NE(ng.diagnostics().find("no such command"), std::string::npos);
    EXPECT_NO_THROW(ng.command("version"));
}

TEST(NgspiceLibrary, RejectsMissingSourceInsteadOfReusingAnEarlierCircuit) {
    NgspiceLib ng;
    ng.load_circuit_lines({"Valid reference", "V1 out 0 1", "R1 out 0 1k", ".op", ".end"});
    ng.run();
    EXPECT_THROW(ng.load_circuit("/neospice-test-nonexistent-directory/missing.cir"),
                 std::runtime_error);
}

TEST(NgspiceLibrary, RejectsAbortedAnalysisWithPartialPlot) {
    NgspiceLib ng;
    // Conflicting ideal sources produce a singular system. The frontend can
    // still return status zero and leave a plot behind after run is aborted.
    ng.load_circuit_lines({"Singular reference", "V1 out 0 1", "V2 out 0 2", ".op", ".end"});
    EXPECT_THROW(ng.run(), std::runtime_error);
    EXPECT_NE(ng.diagnostics().find("aborted"), std::string::npos);
}
