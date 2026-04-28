#pragma once
// Thin wrapper around the system libngspice shared library.
// Provides in-process simulation without fork/exec or file I/O.
// Requires: apt install libngspice0-dev

#include <ngspice/sharedspice.h>

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

class NgspiceLib {
public:
    NgspiceLib() {
        ngSpice_Init(cb_sendchar, cb_sendstat, cb_exit,
                     nullptr, nullptr, nullptr, this);
    }

    ~NgspiceLib() = default;
    NgspiceLib(const NgspiceLib&) = delete;
    NgspiceLib& operator=(const NgspiceLib&) = delete;

    void command(const std::string& cmd) {
        ngSpice_Command(const_cast<char*>(cmd.c_str()));
    }

    void load_circuit(const std::string& filepath) {
        command("source " + filepath);
    }

    void load_circuit_lines(const std::vector<std::string>& lines) {
        std::vector<char*> ptrs;
        ptrs.reserve(lines.size() + 1);
        for (auto& l : lines)
            ptrs.push_back(const_cast<char*>(l.c_str()));
        ptrs.push_back(nullptr);
        ngSpice_Circ(ptrs.data());
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
        command("destroy all");
        command("reset");
    }

private:
    static int cb_sendchar(char*, int, void*) { return 0; }
    static int cb_sendstat(char*, int, void*) { return 0; }
    static int cb_exit(int, bool, bool, int, void*) { return 0; }
};
