#pragma once
#include <string>
#include <unordered_map>

namespace neospice {
double eval_expression(const std::string& expr,
                       const std::unordered_map<std::string, double>& params);
} // namespace neospice
