#include "output/raw_writer.hpp"
#include <fstream>
#include <ctime>
#include <sstream>
#include <vector>
#include <string>
#include <map>

namespace cudaspice {

static std::string current_date_string() {
    std::time_t now = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Y", std::localtime(&now));
    return buf;
}

void write_raw(const std::string& filepath, const TransientResult& result) {
    std::ofstream out(filepath, std::ios::binary);

    // Build ordered variable list: time first, then voltages, then currents
    std::vector<std::string> var_names;
    std::vector<int> var_types; // 0 = time, 1 = voltage, 2 = current
    var_names.push_back("time");
    var_types.push_back(0);

    // Use sorted maps for deterministic ordering
    std::map<std::string, std::vector<double>> sorted_voltages(result.voltages.begin(), result.voltages.end());
    std::map<std::string, std::vector<double>> sorted_currents(result.currents.begin(), result.currents.end());

    for (const auto& [name, _] : sorted_voltages) {
        var_names.push_back(name);
        var_types.push_back(1);
    }
    for (const auto& [name, _] : sorted_currents) {
        var_names.push_back(name);
        var_types.push_back(2);
    }

    std::size_t npoints = result.time.size();
    std::size_t nvars = var_names.size();

    // Write text header
    out << "Title: CudaSPICE Transient Analysis\n";
    out << "Date: " << current_date_string() << "\n";
    out << "Plotname: Transient Analysis\n";
    out << "Flags: real\n";
    out << "No. Variables: " << nvars << "\n";
    out << "No. Points: " << npoints << "\n";
    out << "Variables:\n";
    for (std::size_t i = 0; i < nvars; ++i) {
        const char* type_str = (var_types[i] == 0) ? "time" :
                               (var_types[i] == 1) ? "voltage" : "current";
        out << "\t" << i << "\t" << var_names[i] << "\t" << type_str << "\n";
    }
    out << "Binary:\n";

    // Write binary data: for each point, write all variables as doubles
    for (std::size_t pt = 0; pt < npoints; ++pt) {
        // time
        double val = result.time[pt];
        out.write(reinterpret_cast<const char*>(&val), sizeof(double));
        // voltages
        for (const auto& [name, data] : sorted_voltages) {
            val = data[pt];
            out.write(reinterpret_cast<const char*>(&val), sizeof(double));
        }
        // currents
        for (const auto& [name, data] : sorted_currents) {
            val = data[pt];
            out.write(reinterpret_cast<const char*>(&val), sizeof(double));
        }
    }
}

void write_raw(const std::string& filepath, const DCResult& result) {
    std::ofstream out(filepath, std::ios::binary);

    std::vector<std::string> var_names;
    std::vector<int> var_types; // 1 = voltage, 2 = current

    std::map<std::string, double> sorted_voltages(result.node_voltages.begin(), result.node_voltages.end());
    std::map<std::string, double> sorted_currents(result.branch_currents.begin(), result.branch_currents.end());

    for (const auto& [name, _] : sorted_voltages) {
        var_names.push_back(name);
        var_types.push_back(1);
    }
    for (const auto& [name, _] : sorted_currents) {
        var_names.push_back(name);
        var_types.push_back(2);
    }

    std::size_t nvars = var_names.size();

    out << "Title: CudaSPICE DC Analysis\n";
    out << "Date: " << current_date_string() << "\n";
    out << "Plotname: DC Operating Point\n";
    out << "Flags: real\n";
    out << "No. Variables: " << nvars << "\n";
    out << "No. Points: 1\n";
    out << "Variables:\n";
    for (std::size_t i = 0; i < nvars; ++i) {
        const char* type_str = (var_types[i] == 1) ? "voltage" : "current";
        out << "\t" << i << "\t" << var_names[i] << "\t" << type_str << "\n";
    }
    out << "Binary:\n";

    // Single point
    for (const auto& [name, val] : sorted_voltages) {
        double v = val;
        out.write(reinterpret_cast<const char*>(&v), sizeof(double));
    }
    for (const auto& [name, val] : sorted_currents) {
        double v = val;
        out.write(reinterpret_cast<const char*>(&v), sizeof(double));
    }
}

void write_raw(const std::string& filepath, const ACResult& result) {
    std::ofstream out(filepath, std::ios::binary);

    std::vector<std::string> var_names;
    std::vector<int> var_types; // 0 = frequency, 1 = voltage, 2 = current
    var_names.push_back("frequency");
    var_types.push_back(0);

    std::map<std::string, std::vector<std::complex<double>>> sorted_voltages(result.voltages.begin(), result.voltages.end());
    std::map<std::string, std::vector<std::complex<double>>> sorted_currents(result.currents.begin(), result.currents.end());

    for (const auto& [name, _] : sorted_voltages) {
        var_names.push_back(name);
        var_types.push_back(1);
    }
    for (const auto& [name, _] : sorted_currents) {
        var_names.push_back(name);
        var_types.push_back(2);
    }

    std::size_t npoints = result.frequency.size();
    std::size_t nvars = var_names.size();

    out << "Title: CudaSPICE AC Analysis\n";
    out << "Date: " << current_date_string() << "\n";
    out << "Plotname: AC Analysis\n";
    out << "Flags: complex\n";
    out << "No. Variables: " << nvars << "\n";
    out << "No. Points: " << npoints << "\n";
    out << "Variables:\n";
    for (std::size_t i = 0; i < nvars; ++i) {
        const char* type_str = (var_types[i] == 0) ? "frequency" :
                               (var_types[i] == 1) ? "voltage" : "current";
        out << "\t" << i << "\t" << var_names[i] << "\t" << type_str << "\n";
    }
    out << "Binary:\n";

    // For complex: each variable is written as (re, im) pair of doubles
    for (std::size_t pt = 0; pt < npoints; ++pt) {
        // frequency as (freq, 0.0)
        double re = result.frequency[pt];
        double im = 0.0;
        out.write(reinterpret_cast<const char*>(&re), sizeof(double));
        out.write(reinterpret_cast<const char*>(&im), sizeof(double));
        // voltages
        for (const auto& [name, data] : sorted_voltages) {
            re = data[pt].real();
            im = data[pt].imag();
            out.write(reinterpret_cast<const char*>(&re), sizeof(double));
            out.write(reinterpret_cast<const char*>(&im), sizeof(double));
        }
        // currents
        for (const auto& [name, data] : sorted_currents) {
            re = data[pt].real();
            im = data[pt].imag();
            out.write(reinterpret_cast<const char*>(&re), sizeof(double));
            out.write(reinterpret_cast<const char*>(&im), sizeof(double));
        }
    }
}

} // namespace cudaspice
