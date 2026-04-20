#pragma once
#include "core/transient.hpp"
#include "core/dc.hpp"
#include "core/ac.hpp"
#include <string>

namespace neospice {
struct SimulationResult;
void write_raw(const std::string& filepath, const TransientResult& result,
               const std::string& title = "");
void write_raw(const std::string& filepath, const DCResult& result,
               const std::string& title = "");
void write_raw(const std::string& filepath, const ACResult& result,
               const std::string& title = "");
void write_raw(const std::string& filepath, const DCSweepResult& result,
               const std::string& title = "");
void write_raw(const std::string& filepath, const SimulationResult& result,
               const std::string& title = "");
} // namespace neospice
