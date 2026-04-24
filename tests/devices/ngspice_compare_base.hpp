#pragma once

#include "api/neospice.hpp"
#include "framework/ngspice_runner.hpp"
#include "framework/comparator.hpp"

#include <gtest/gtest.h>
#include <memory>

using namespace neospice;

/// Shared base fixture for device comparison tests.
/// Provides a pre-configured NgspiceRunner and Simulator.
class NgspiceComparisonTest : public ::testing::Test {
protected:
    void SetUp() override {
        ngspice_ = std::make_unique<NgspiceRunner>(NGSPICE_BINARY);
    }
    std::unique_ptr<NgspiceRunner> ngspice_;
    Simulator sim_;
};
