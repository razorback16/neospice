#pragma once
// Thin wrapper around the system libngspice shared library.
// Provides in-process simulation. load_circuit() reads a file through ngspice;
// load_circuit_lines() accepts an already materialized netlist.
// Requires: apt install libngspice0-dev

#include <ngspice/sharedspice.h>

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <mutex>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

// libngspice is process-global: these wrappers do not own independent circuits.
// Use synchronous operations in one thread; parallel tests use separate processes.
class NgspiceLib {
    struct State {
        std::once_flag initialized;
        mutable std::mutex diagnostics_mutex_;
        std::string diagnostics_;
        bool operation_failed_ = false;
        bool exited_ = false;
        bool has_circuit_ = false;
    };
    static State& state() {
        // Callback storage outlives individual wrappers. Reinitializing ngspice
        // for each wrapper leaks its process-global frontend allocations.
        static State value;
        return value;
    }
public:
    NgspiceLib() {
        std::call_once(state().initialized, [this] {
            const int status = ngSpice_Init(cb_sendchar, cb_sendstat, cb_exit,
                                           nullptr, nullptr, nullptr, &state());
            check_status("initialization", status);
        });
    }

    ~NgspiceLib() {
        // Never throw across destruction, including after a rejected analysis.
        try { reset(); } catch (...) {}
    }
    NgspiceLib(const NgspiceLib&) = delete;
    NgspiceLib& operator=(const NgspiceLib&) = delete;

    void command(const std::string& cmd) {
        begin_operation();
        const int status = ngSpice_Command(const_cast<char*>(cmd.c_str()));
        check_status(cmd, status);
    }

    void load_circuit(const std::string& filepath) {
        reset();
        // Reject unreadable inputs before calling the source handler; the old circuit has
        // already been discarded, so a later run cannot reuse stale results.
        std::ifstream input(filepath);
        if (!input.good()) {
            begin_operation();
            std::lock_guard lock(state().diagnostics_mutex_);
            state().diagnostics_ = "cannot read source file: " + filepath;
            throw std::runtime_error(state().diagnostics_);
        }
        state().has_circuit_ = true; // cleanup also required if parsing fails
        // The shared API's source handler consumes the remainder literally;
        // adding shell-style quotes makes them part of the filename.
        command("source " + filepath);
    }

    void load_circuit_lines(const std::vector<std::string>& lines) {
        reset();
        std::vector<char*> ptrs;
        ptrs.reserve(lines.size() + 1);
        for (auto& l : lines)
            ptrs.push_back(const_cast<char*>(l.c_str()));
        ptrs.push_back(nullptr);
        begin_operation();
        state().has_circuit_ = true;
        const int status = ngSpice_Circ(ptrs.data());
        check_status("load circuit lines", status);
    }

    void run() { command("run"); }
    void op()  { command("op"); }

    void ac(const std::string& mode, int npoints, double fstart, double fstop) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "ac %s %d %g %g",
                      mode.c_str(), npoints, fstart, fstop);
        command(buf);
    }

    void tran(double tstep, double tstop) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "tran %g %g", tstep, tstop);
        command(buf);
    }

    char* cur_plot() { return ngSpice_CurPlot(); }

    char** all_vecs(const std::string& plot) {
        return ngSpice_AllVecs(const_cast<char*>(plot.c_str()));
    }

    pvector_info get_vec_info(const std::string& name) {
        return ngGet_Vec_Info(const_cast<char*>(name.c_str()));
    }

    char** all_plots() { return ngSpice_AllPlots(); }

    void reset() {
        if (state().has_circuit_) {
            command("destroy all");
            command("remcirc");
            state().has_circuit_ = false;
        }
    }

    std::string diagnostics() const {
        std::lock_guard lock(state().diagnostics_mutex_);
        return state().diagnostics_;
    }

private:
    void begin_operation() {
        auto& self = state();
        std::lock_guard lock(self.diagnostics_mutex_);
        if (self.exited_) throw std::runtime_error("ngspice requested library detachment");
        self.diagnostics_.clear();
        self.operation_failed_ = false;
    }

    void check_status(const std::string& operation, int status) const {
        auto& self = state();
        std::lock_guard lock(self.diagnostics_mutex_);
        if (status != 0 || self.operation_failed_ || self.exited_)
            throw std::runtime_error("ngspice " + operation + " failed (status " +
                                     std::to_string(status) + "): " + self.diagnostics_);
    }

    static int cb_sendchar(char* message, int, void* user) {
        auto& self = *static_cast<State*>(user);
        std::lock_guard lock(self.diagnostics_mutex_);
        const std::string text = message ? message : "";
        self.diagnostics_ += text + '\n';
        std::string lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        // ngSpice_Command often returns zero after frontend/analysis errors.
        // Preserve warnings about recoverable gmin/source-stepping attempts;
        // reject explicit errors and terminal analysis failures.
        // ngspice 47 ltraset.c labels its default-C=0 notice "Fatal error"
        // but deliberately continues (the E_BADPARM return is commented out).
        // RG lines legitimately use C=0. A nonzero API status or later abort
        // still fails; retain this exact notice in diagnostics.
        const bool ltra_default_c = lower.find(
            "lossy line parallel capacitance not given, assumed zero") != std::string::npos;
        self.operation_failed_ |= lower.starts_with("stderr error") ||
            (lower.starts_with("stderr fatal") && !ltra_default_c) ||
            lower.find("doanalyses:") != std::string::npos ||
            lower.find("simulation(s) aborted") != std::string::npos ||
            lower.find("no such command") != std::string::npos ||
            lower.find("no such file or directory") != std::string::npos;
        return 0;
    }
    static int cb_sendstat(char*, int, void*) { return 0; }
    static int cb_exit(int, bool, bool, int, void* user) {
        auto& self = *static_cast<State*>(user);
        std::lock_guard lock(self.diagnostics_mutex_);
        self.exited_ = true;
        return 0;
    }
};
