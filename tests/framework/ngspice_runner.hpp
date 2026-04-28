#pragma once
#include "core/dc.hpp"
#include "core/transient.hpp"
#include "core/ac.hpp"
#include "framework/ngspice_lib.hpp"
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
    NgspiceRunner();
    DCResult run_dc(const std::string& cir_path);
    DCSweepResult run_dc_sweep(const std::string& cir_path);
    TransientResult run_transient(const std::string& cir_path);
    ACResult run_ac(const std::string& cir_path);
    NgspiceNoiseResult run_noise(const std::string& cir_path);

private:
    NgspiceLib ng_;
    std::string find_plot(const std::string& prefix);
    std::vector<std::string> vec_names(const std::string& plot);
    static std::string normalize_name(const std::string& name);
};

} // namespace neospice
