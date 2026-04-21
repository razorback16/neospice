#pragma once
#include <complex>
#include <string>
#include <vector>

namespace neospice {

struct Circuit;

enum class PZType { POLES, ZEROS, BOTH };
enum class PZTransferType { VOLTAGE, CURRENT };

struct PZResult {
    std::vector<std::complex<double>> poles;
    std::vector<std::complex<double>> zeros;
    PZType type;
    PZTransferType transfer_type;
    std::string input_pos, input_neg;
    std::string output_pos, output_neg;
};

PZResult solve_pz(Circuit& ckt,
                  const std::string& in_pos, const std::string& in_neg,
                  const std::string& out_pos, const std::string& out_neg,
                  PZTransferType transfer, PZType type);

} // namespace neospice
