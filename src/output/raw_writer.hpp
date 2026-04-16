#pragma once
#include "core/transient.hpp"
#include "core/dc.hpp"
#include "core/ac.hpp"
#include <string>

namespace cudaspice {
void write_raw(const std::string& filepath, const TransientResult& result);
void write_raw(const std::string& filepath, const DCResult& result);
void write_raw(const std::string& filepath, const ACResult& result);
} // namespace cudaspice
