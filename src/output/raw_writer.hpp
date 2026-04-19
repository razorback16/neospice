#pragma once
#include "core/transient.hpp"
#include "core/dc.hpp"
#include "core/ac.hpp"
#include <string>

namespace neospice {
void write_raw(const std::string& filepath, const TransientResult& result);
void write_raw(const std::string& filepath, const DCResult& result);
void write_raw(const std::string& filepath, const ACResult& result);
void write_raw(const std::string& filepath, const DCSweepResult& result);
} // namespace neospice
