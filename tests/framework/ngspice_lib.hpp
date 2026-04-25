#pragma once
// Thin wrapper around libngspice shared library (dlopen-based).
// Provides in-process simulation without fork/exec or file I/O.

#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <stdexcept>
#include <string>
#include <vector>

struct ngcomplex_t {
    double cx_real;
    double cx_imag;
};

struct vector_info {
    char *v_name;
    int v_type;
    short v_flags;
    double *v_realdata;
    ngcomplex_t *v_compdata;
    int v_length;
};

class NgspiceLib {
public:
    explicit NgspiceLib(const std::string& lib_path,
                        const std::string& spice_lib_dir = "") {
        if (!spice_lib_dir.empty())
            setenv("SPICE_LIB_DIR", spice_lib_dir.c_str(), 1);

        handle_ = dlopen(lib_path.c_str(), RTLD_NOW);
        if (!handle_)
            throw std::runtime_error(std::string("dlopen: ") + dlerror());

        fn_init_    = reinterpret_cast<InitFn>(load("ngSpice_Init"));
        fn_command_ = reinterpret_cast<CommandFn>(load("ngSpice_Command"));
        fn_circ_    = reinterpret_cast<CircFn>(load("ngSpice_Circ"));
        fn_vec_     = reinterpret_cast<VecInfoFn>(load("ngGet_Vec_Info"));
        fn_curplot_ = reinterpret_cast<CurPlotFn>(load("ngSpice_CurPlot"));
        fn_allvecs_ = reinterpret_cast<AllVecsFn>(load("ngSpice_AllVecs"));

        fn_init_(cb_sendchar, cb_sendstat, cb_exit,
                 nullptr, nullptr, nullptr, this);
    }

    ~NgspiceLib() {
        if (handle_) dlclose(handle_);
    }

    NgspiceLib(const NgspiceLib&) = delete;
    NgspiceLib& operator=(const NgspiceLib&) = delete;

    void command(const std::string& cmd) {
        fn_command_(const_cast<char*>(cmd.c_str()));
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
        fn_circ_(ptrs.data());
    }

    void run() { command("run"); }
    void op()  { command("op"); }

    void dc() {
        command("op");
    }

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

    char* cur_plot() { return fn_curplot_(); }

    char** all_vecs(const std::string& plot) {
        return fn_allvecs_(const_cast<char*>(plot.c_str()));
    }

    vector_info* get_vec_info(const std::string& name) {
        return fn_vec_(const_cast<char*>(name.c_str()));
    }

    // Destroy internal circuit state so the next source starts fresh.
    void reset() {
        command("destroy all");
        command("reset");
    }

private:
    void* handle_ = nullptr;

    using InitFn    = int(*)(int(*)(char*,int,void*),
                             int(*)(char*,int,void*),
                             int(*)(int,bool,bool,int,void*),
                             int(*)(void*,int,int,void*),
                             int(*)(void*,int,void*),
                             int(*)(bool,int,void*),
                             void*);
    using CommandFn = int(*)(char*);
    using CircFn    = int(*)(char**);
    using VecInfoFn = vector_info*(*)(char*);
    using CurPlotFn = char*(*)();
    using AllVecsFn = char**(*)(char*);

    InitFn    fn_init_    = nullptr;
    CommandFn fn_command_ = nullptr;
    CircFn    fn_circ_    = nullptr;
    VecInfoFn fn_vec_     = nullptr;
    CurPlotFn fn_curplot_ = nullptr;
    AllVecsFn fn_allvecs_ = nullptr;

    void* load(const char* sym) {
        void* p = dlsym(handle_, sym);
        if (!p) throw std::runtime_error(std::string("dlsym: ") + dlerror());
        return p;
    }

    static int cb_sendchar(char*, int, void*) { return 0; }
    static int cb_sendstat(char*, int, void*) { return 0; }
    static int cb_exit(int, bool, bool, int, void*) { return 0; }
};
