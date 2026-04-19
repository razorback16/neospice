#pragma once
#include "core/dc.hpp"
#include "core/transient.hpp"
#include "core/ac.hpp"
#include <complex>
#include <string>
#include <vector>

namespace neospice {

struct NgspiceNoiseResult {
    std::vector<double> frequency;
    std::vector<double> onoise_spectrum;  // V/sqrt(Hz)
    std::vector<double> inoise_spectrum;  // V/sqrt(Hz)
};

class NgspiceRunner {
public:
    explicit NgspiceRunner(const std::string& binary_path);
    DCResult run_dc(const std::string& cir_path);
    TransientResult run_transient(const std::string& cir_path);
    ACResult run_ac(const std::string& cir_path);
    NgspiceNoiseResult run_noise(const std::string& cir_path);

private:
    std::string binary_;
    std::string run_batch(const std::string& cir_path);

    struct RawData {
        std::string plot_type;
        int num_vars = 0;
        int num_points = 0;
        std::vector<std::string> var_names;
        std::vector<std::vector<double>> real_data;
        std::vector<std::vector<std::complex<double>>> complex_data;
        bool is_complex = false;
    };
    RawData parse_raw(const std::string& raw_path);
};

} // namespace neospice
