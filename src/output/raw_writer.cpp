#include "output/raw_writer.hpp"
#include "api/neospice.hpp"
#include <cstdio>
#include <ctime>
#include <cstring>
#include <vector>
#include <string>

namespace neospice {

static std::string current_date_string() {
    std::time_t now = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Y", std::localtime(&now));
    return buf;
}

// ---------------------------------------------------------------------------
// Internal helpers that write a single plot to an already-open FILE*.
// ---------------------------------------------------------------------------

static void write_plot_transient(FILE* fp, const TransientResult& result,
                                 const std::string& title) {
    std::vector<std::string> var_names;
    std::vector<int> var_types;
    var_names.push_back("time");
    var_types.push_back(0);

    for (const auto& [name, _] : result.voltages) {
        var_names.push_back(name);
        var_types.push_back(1);
    }
    for (const auto& [name, _] : result.currents) {
        var_names.push_back(name);
        var_types.push_back(2);
    }

    std::size_t npoints = result.time.size();
    std::size_t nvars = var_names.size();

    std::fprintf(fp, "Title: %s\n", title.c_str());
    std::fprintf(fp, "Date: %s\n", current_date_string().c_str());
    std::fprintf(fp, "Plotname: Transient Analysis\n");
    std::fprintf(fp, "Flags: real\n");
    std::fprintf(fp, "No. Variables: %zu\n", nvars);
    std::fprintf(fp, "No. Points: %zu\n", npoints);
    std::fprintf(fp, "Variables:\n");
    for (std::size_t i = 0; i < nvars; ++i) {
        const char* type_str = (var_types[i] == 0) ? "time" :
                               (var_types[i] == 1) ? "voltage" : "current";
        std::fprintf(fp, "\t%zu\t%s\t%s\n", i, var_names[i].c_str(), type_str);
    }
    std::fprintf(fp, "Binary:\n");

    std::vector<const std::vector<double>*> col_ptrs;
    col_ptrs.push_back(&result.time);
    for (const auto& [name, data] : result.voltages)
        col_ptrs.push_back(&data);
    for (const auto& [name, data] : result.currents)
        col_ptrs.push_back(&data);

    std::vector<double> row(nvars);
    for (std::size_t pt = 0; pt < npoints; ++pt) {
        for (std::size_t c = 0; c < col_ptrs.size(); ++c)
            row[c] = (*col_ptrs[c])[pt];
        std::fwrite(row.data(), sizeof(double), nvars, fp);
    }
}

static void write_plot_dc(FILE* fp, const DCResult& result,
                          const std::string& title) {
    std::vector<std::string> var_names;
    std::vector<int> var_types;

    for (const auto& [name, _] : result.node_voltages) {
        var_names.push_back(name);
        var_types.push_back(1);
    }
    for (const auto& [name, _] : result.branch_currents) {
        var_names.push_back(name);
        var_types.push_back(2);
    }

    std::size_t nvars = var_names.size();

    std::fprintf(fp, "Title: %s\n", title.c_str());
    std::fprintf(fp, "Date: %s\n", current_date_string().c_str());
    std::fprintf(fp, "Plotname: Operating Point\n");
    std::fprintf(fp, "Flags: real\n");
    std::fprintf(fp, "No. Variables: %zu\n", nvars);
    std::fprintf(fp, "No. Points: 1\n");
    std::fprintf(fp, "Variables:\n");
    for (std::size_t i = 0; i < nvars; ++i) {
        const char* type_str = (var_types[i] == 1) ? "voltage" : "current";
        std::fprintf(fp, "\t%zu\t%s\t%s\n", i, var_names[i].c_str(), type_str);
    }
    std::fprintf(fp, "Binary:\n");

    for (const auto& [name, val] : result.node_voltages) {
        double v = val;
        std::fwrite(&v, sizeof(double), 1, fp);
    }
    for (const auto& [name, val] : result.branch_currents) {
        double v = val;
        std::fwrite(&v, sizeof(double), 1, fp);
    }
}

static void write_plot_ac(FILE* fp, const ACResult& result,
                          const std::string& title) {
    std::vector<std::string> var_names;
    std::vector<int> var_types;
    var_names.push_back("frequency");
    var_types.push_back(0);

    for (const auto& [name, _] : result.voltages) {
        var_names.push_back(name);
        var_types.push_back(1);
    }
    for (const auto& [name, _] : result.currents) {
        var_names.push_back(name);
        var_types.push_back(2);
    }

    std::size_t npoints = result.frequency.size();
    std::size_t nvars = var_names.size();

    std::fprintf(fp, "Title: %s\n", title.c_str());
    std::fprintf(fp, "Date: %s\n", current_date_string().c_str());
    std::fprintf(fp, "Plotname: AC Analysis\n");
    std::fprintf(fp, "Flags: complex\n");
    std::fprintf(fp, "No. Variables: %zu\n", nvars);
    std::fprintf(fp, "No. Points: %zu\n", npoints);
    std::fprintf(fp, "Variables:\n");
    for (std::size_t i = 0; i < nvars; ++i) {
        const char* type_str = (var_types[i] == 0) ? "frequency" :
                               (var_types[i] == 1) ? "voltage" : "current";
        if (var_types[i] == 0)
            std::fprintf(fp, "\t%zu\t%s\t%s\tgrid=3\n", i, var_names[i].c_str(), type_str);
        else
            std::fprintf(fp, "\t%zu\t%s\t%s\n", i, var_names[i].c_str(), type_str);
    }
    std::fprintf(fp, "Binary:\n");

    std::vector<const std::vector<std::complex<double>>*> col_ptrs;
    for (const auto& [name, data] : result.voltages)
        col_ptrs.push_back(&data);
    for (const auto& [name, data] : result.currents)
        col_ptrs.push_back(&data);

    std::vector<double> row(2 * nvars);
    for (std::size_t pt = 0; pt < npoints; ++pt) {
        row[0] = result.frequency[pt];
        row[1] = 0.0;
        for (std::size_t c = 0; c < col_ptrs.size(); ++c) {
            row[2 + 2*c]     = (*col_ptrs[c])[pt].real();
            row[2 + 2*c + 1] = (*col_ptrs[c])[pt].imag();
        }
        std::fwrite(row.data(), sizeof(double), 2 * nvars, fp);
    }
}

static void write_plot_dc_sweep(FILE* fp, const DCSweepResult& result,
                                const std::string& title) {
    std::vector<std::string> var_names;
    std::vector<int> var_types;
    var_names.push_back(result.sweep_var);
    var_types.push_back(0);

    for (const auto& [name, _] : result.voltages) {
        var_names.push_back(name);
        var_types.push_back(1);
    }
    for (const auto& [name, _] : result.currents) {
        var_names.push_back(name);
        var_types.push_back(2);
    }

    std::size_t npoints = result.sweep_values.size();
    std::size_t nvars = var_names.size();

    std::fprintf(fp, "Title: %s\n", title.c_str());
    std::fprintf(fp, "Date: %s\n", current_date_string().c_str());
    std::fprintf(fp, "Plotname: DC Transfer Characteristic\n");
    std::fprintf(fp, "Flags: real\n");
    std::fprintf(fp, "No. Variables: %zu\n", nvars);
    std::fprintf(fp, "No. Points: %zu\n", npoints);
    std::fprintf(fp, "Variables:\n");
    for (std::size_t i = 0; i < nvars; ++i) {
        const char* type_str = (var_types[i] == 0) ? "sweep" :
                               (var_types[i] == 1) ? "voltage" : "current";
        std::fprintf(fp, "\t%zu\t%s\t%s\n", i, var_names[i].c_str(), type_str);
    }
    std::fprintf(fp, "Binary:\n");

    std::vector<const std::vector<double>*> col_ptrs;
    col_ptrs.push_back(&result.sweep_values);
    for (const auto& [name, data] : result.voltages)
        col_ptrs.push_back(&data);
    for (const auto& [name, data] : result.currents)
        col_ptrs.push_back(&data);

    std::vector<double> row(nvars);
    for (std::size_t pt = 0; pt < npoints; ++pt) {
        for (std::size_t c = 0; c < col_ptrs.size(); ++c)
            row[c] = (*col_ptrs[c])[pt];
        std::fwrite(row.data(), sizeof(double), nvars, fp);
    }
}

// ---------------------------------------------------------------------------
// Public API: single-analysis overloads (open file, write one plot, close).
// ---------------------------------------------------------------------------

void write_raw(const std::string& filepath, const TransientResult& result,
               const std::string& title) {
    FILE* fp = std::fopen(filepath.c_str(), "wb");
    if (!fp) return;
    char stream_buf[256 * 1024];
    std::setvbuf(fp, stream_buf, _IOFBF, sizeof(stream_buf));
    write_plot_transient(fp, result, title.empty() ? "neospice Transient Analysis" : title);
    std::fclose(fp);
}

void write_raw(const std::string& filepath, const DCResult& result,
               const std::string& title) {
    FILE* fp = std::fopen(filepath.c_str(), "wb");
    if (!fp) return;
    write_plot_dc(fp, result, title.empty() ? "neospice DC Analysis" : title);
    std::fclose(fp);
}

void write_raw(const std::string& filepath, const ACResult& result,
               const std::string& title) {
    FILE* fp = std::fopen(filepath.c_str(), "wb");
    if (!fp) return;
    char stream_buf[256 * 1024];
    std::setvbuf(fp, stream_buf, _IOFBF, sizeof(stream_buf));
    write_plot_ac(fp, result, title.empty() ? "neospice AC Analysis" : title);
    std::fclose(fp);
}

void write_raw(const std::string& filepath, const DCSweepResult& result,
               const std::string& title) {
    FILE* fp = std::fopen(filepath.c_str(), "wb");
    if (!fp) return;
    char stream_buf[256 * 1024];
    std::setvbuf(fp, stream_buf, _IOFBF, sizeof(stream_buf));
    write_plot_dc_sweep(fp, result, title.empty() ? "neospice DC Sweep Analysis" : title);
    std::fclose(fp);
}

// ---------------------------------------------------------------------------
// Multi-plot: writes all present analyses to a single concatenated raw file.
// Plot order matches ngspice: AC/Tran/DCSweep first, then Operating Point.
// ---------------------------------------------------------------------------

void write_raw(const std::string& filepath, const SimulationResult& result,
               const std::string& title) {
    FILE* fp = std::fopen(filepath.c_str(), "wb");
    if (!fp) return;
    char stream_buf[256 * 1024];
    std::setvbuf(fp, stream_buf, _IOFBF, sizeof(stream_buf));

    std::string t = title.empty() ? "neospice" : title;

    if (auto* p = std::get_if<ACResult>(&result.analysis))
        write_plot_ac(fp, *p, t);
    if (auto* p = std::get_if<TransientResult>(&result.analysis))
        write_plot_transient(fp, *p, t);
    if (auto* p = std::get_if<DCSweepResult>(&result.analysis))
        write_plot_dc_sweep(fp, *p, t);
    if (auto* p = std::get_if<DCResult>(&result.analysis))
        write_plot_dc(fp, *p, t);

    std::fclose(fp);
}

} // namespace neospice
