#include "framework/ngspice_runner.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <filesystem>

namespace neospice {

NgspiceRunner::NgspiceRunner(const std::string& binary_path)
    : binary_(binary_path) {}

std::string NgspiceRunner::run_batch(const std::string& cir_path) {
    // Create a temporary directory for output
    auto tmp_dir = std::filesystem::temp_directory_path() / "neospice_ngspice_XXXXXX";
    std::string tmp_dir_str = tmp_dir.string();
    char* dir = mkdtemp(tmp_dir_str.data());
    if (!dir) {
        throw std::runtime_error("Failed to create temp directory");
    }
    std::string raw_path = std::string(dir) + "/output.raw";

    // Run ngspice in batch mode
    std::string cmd = binary_ + " -b " + cir_path + " -r " + raw_path + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("Failed to run ngspice");
    }

    // Read all output (for debugging)
    char buffer[256];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }

    int status = pclose(pipe);
    if (!std::filesystem::exists(raw_path)) {
        throw std::runtime_error("ngspice did not produce output .raw file. Output:\n" + output);
    }

    return raw_path;
}

NgspiceRunner::RawData NgspiceRunner::parse_raw(const std::string& raw_path) {
    RawData data;
    std::ifstream file(raw_path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open raw file: " + raw_path);
    }

    std::string line;
    bool in_variables = false;
    int var_index = 0;

    // Parse header
    while (std::getline(file, line)) {
        // Remove trailing \r if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.find("Plotname:") == 0) {
            data.plot_type = line.substr(10);
        } else if (line.find("Flags:") == 0) {
            std::string flags = line.substr(7);
            std::transform(flags.begin(), flags.end(), flags.begin(), ::tolower);
            data.is_complex = (flags.find("complex") != std::string::npos);
        } else if (line.find("No. Variables:") == 0) {
            data.num_vars = std::stoi(line.substr(15));
        } else if (line.find("No. Points:") == 0) {
            data.num_points = std::stoi(line.substr(12));
        } else if (line.find("Variables:") == 0) {
            in_variables = true;
            continue;
        } else if (line.find("Binary:") == 0) {
            break; // Binary data follows immediately
        }

        if (in_variables && !line.empty() && (line[0] == '\t' || line[0] == ' ')) {
            // Parse variable line: <index> <name> <type>
            std::istringstream iss(line);
            int idx;
            std::string name, type;
            iss >> idx >> name >> type;
            // Lowercase the name for consistent lookup
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            data.var_names.push_back(name);
            var_index++;
        }
    }

    // Read binary data
    if (data.is_complex) {
        data.complex_data.resize(data.num_vars);
        for (auto& v : data.complex_data) {
            v.resize(data.num_points);
        }
        for (int p = 0; p < data.num_points; ++p) {
            for (int v = 0; v < data.num_vars; ++v) {
                double re, im;
                file.read(reinterpret_cast<char*>(&re), sizeof(double));
                file.read(reinterpret_cast<char*>(&im), sizeof(double));
                data.complex_data[v][p] = std::complex<double>(re, im);
            }
        }
    } else {
        data.real_data.resize(data.num_vars);
        for (auto& v : data.real_data) {
            v.resize(data.num_points);
        }
        for (int p = 0; p < data.num_points; ++p) {
            for (int v = 0; v < data.num_vars; ++v) {
                double val;
                file.read(reinterpret_cast<char*>(&val), sizeof(double));
                data.real_data[v][p] = val;
            }
        }
    }

    return data;
}

DCResult NgspiceRunner::run_dc(const std::string& cir_path) {
    std::string raw_path = run_batch(cir_path);
    RawData raw = parse_raw(raw_path);
    std::filesystem::remove(raw_path);
    std::filesystem::remove(std::filesystem::path(raw_path).parent_path());

    DCResult result;
    for (int v = 0; v < raw.num_vars; ++v) {
        const auto& name = raw.var_names[v];
        // Last point is the DC operating point
        int last = raw.num_points - 1;
        double value = raw.is_complex ? raw.complex_data[v][last].real()
                                      : raw.real_data[v][last];

        if (name.find("v(") == 0 || name == "v-sweep") {
            result.node_voltages[name] = value;
        } else if (name.find("i(") == 0 || name.find("#branch") != std::string::npos) {
            result.branch_currents[name] = value;
        } else {
            // Store voltages for node names too
            result.node_voltages[name] = value;
        }
    }

    return result;
}

TransientResult NgspiceRunner::run_transient(const std::string& cir_path) {
    std::string raw_path = run_batch(cir_path);
    RawData raw = parse_raw(raw_path);
    std::filesystem::remove(raw_path);
    std::filesystem::remove(std::filesystem::path(raw_path).parent_path());

    TransientResult result;

    for (int v = 0; v < raw.num_vars; ++v) {
        const auto& name = raw.var_names[v];
        if (name == "time") {
            result.time = raw.real_data[v];
        } else if (name.find("v(") == 0) {
            result.voltages[name] = raw.real_data[v];
        } else if (name.find("i(") == 0 || name.find("#branch") != std::string::npos) {
            result.currents[name] = raw.real_data[v];
        } else {
            result.voltages[name] = raw.real_data[v];
        }
    }

    return result;
}

ACResult NgspiceRunner::run_ac(const std::string& cir_path) {
    std::string raw_path = run_batch(cir_path);
    RawData raw = parse_raw(raw_path);
    std::filesystem::remove(raw_path);
    std::filesystem::remove(std::filesystem::path(raw_path).parent_path());

    ACResult result;

    for (int v = 0; v < raw.num_vars; ++v) {
        const auto& name = raw.var_names[v];
        if (name == "frequency") {
            // Frequency is stored as complex; extract real part
            result.frequency.resize(raw.num_points);
            for (int p = 0; p < raw.num_points; ++p) {
                result.frequency[p] = raw.complex_data[v][p].real();
            }
        } else if (name.find("v(") == 0) {
            result.voltages[name] = raw.complex_data[v];
        } else if (name.find("i(") == 0 || name.find("#branch") != std::string::npos) {
            result.currents[name] = raw.complex_data[v];
        } else {
            result.voltages[name] = raw.complex_data[v];
        }
    }

    return result;
}

} // namespace neospice
