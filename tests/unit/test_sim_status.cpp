#include <gtest/gtest.h>
#include "core/sim_status.hpp"

using namespace neospice;

TEST(SimStatus, DefaultIsEmpty) {
    SimStatus s;
    EXPECT_TRUE(s.converged);
    EXPECT_EQ(s.iterations, 0);
    EXPECT_EQ(s.convergence_method, ConvergenceMethod::DIRECT);
    EXPECT_TRUE(s.warnings.empty());
}

TEST(SimStatus, AddWarning) {
    SimStatus s;
    s.warnings.push_back("gmin stepping used");
    EXPECT_EQ(s.warnings.size(), 1u);
    EXPECT_EQ(s.warnings[0], "gmin stepping used");
}

TEST(SimStatus, MethodToString) {
    EXPECT_EQ(std::string(convergence_method_name(ConvergenceMethod::DIRECT)), "direct");
    EXPECT_EQ(std::string(convergence_method_name(ConvergenceMethod::GMIN_STEPPING)), "gmin-stepping");
    EXPECT_EQ(std::string(convergence_method_name(ConvergenceMethod::SOURCE_STEPPING)), "source-stepping");
    EXPECT_EQ(std::string(convergence_method_name(ConvergenceMethod::PSEUDO_TRANSIENT)), "pseudo-transient");
}
