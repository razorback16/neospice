// bench_solver_throughput — solve-phase throughput on large, scalable circuits.
//
// Motivation (docs/performance-analysis.md Priority 4, "the measurement gap"):
// the Phase 1-3 parser wins were measured on tiny .op fixtures dominated by
// parse/startup. This benchmark instead generates genuinely LARGE circuits
// in-memory and reports SOLVE time isolated from PARSE time, so future parser
// optimizations cannot mask a solver regression (and vice-versa).
//
// Two scalable circuit classes are exercised:
//   1) LINEAR  — a 2-D resistor mesh (R grid). Purely linear, so DC converges in
//      one Newton step: this isolates symbolic + numeric LU factorization and the
//      triangular solve (the sparse-LU hot path).
//   2) NONLINEAR — a chain of diode+resistor stages. Each stage adds a nonlinear
//      junction, so DC needs multiple Newton iterations: this isolates Newton
//      iteration count and per-iteration device evaluation.
//
// Phase isolation via the *public* API:
//   - PARSE: time Simulator::parse(netlist_text) (build + finalize).
//   - SOLVE: time Simulator::run_dc(ckt).
//   - Newton iterations + convergence method come from DCResult::status
//     (SimStatus::iterations / convergence_method).
//
// Finer granularity (symbolic-factor vs numeric-factor vs triangular-solve vs
// device-eval microseconds) is NOT exposed by the public API: newton_solve()
// and NeoSolver do not return per-phase timers. Splitting those cleanly would
// need lightweight instrumentation hooks inside newton_solve()/NeoSolver
// (e.g. an optional out-param timing struct). That is intentionally left as a
// follow-up — see docs/performance-analysis.md — to avoid adding timers to the
// solver hot path here. The existing scratch profiler tests/bench/
// bench_newton_profile.cpp shows what such a manual breakdown looks like by
// re-implementing the Newton loop, but that duplicates solver internals and is
// not representative of the production path.

#include "api/neospice.hpp"
#include "core/circuit.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace neospice;
using Clock = std::chrono::high_resolution_clock;

// ---------------------------------------------------------------------------
// Netlist generators
// ---------------------------------------------------------------------------

// 2-D resistor mesh: a (w x h) grid of nodes, each connected to its right and
// bottom neighbour by a 1k resistor. A 1V source drives the top-left corner,
// the bottom-right corner is grounded. Purely linear -> 1 Newton step.
// Node count ~= w*h; nnz grows with the mesh connectivity.
static std::string make_resistor_mesh(int w, int h) {
    std::string s = "* 2D resistor mesh " + std::to_string(w) + "x" + std::to_string(h) + "\n";
    s += "Vin n0_0 0 DC 1\n";
    int rcount = 0;
    // Ground the far corner so the system is non-singular. Map that single node
    // name to "0" everywhere it appears so no node is left floating.
    auto nodename = [w, h](int x, int y) -> std::string {
        if (x == w - 1 && y == h - 1) return "0";
        return "n" + std::to_string(x) + "_" + std::to_string(y);
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (x + 1 < w)
                s += "R" + std::to_string(rcount++) + " " + nodename(x, y) + " " +
                     nodename(x + 1, y) + " 1k\n";
            if (y + 1 < h)
                s += "R" + std::to_string(rcount++) + " " + nodename(x, y) + " " +
                     nodename(x, y + 1) + " 1k\n";
        }
    }
    s += ".op\n.end\n";
    return s;
}

// Diode + resistor ladder: a chain of `stages` stages. Each stage is a diode in
// series with a resistor pulling toward ground, forming a nonlinear divider
// chain. Driven by a 5V source. Each diode is a nonlinear junction so DC needs
// several Newton iterations -> stresses Newton + device eval.
static std::string make_diode_ladder(int stages) {
    std::string s = "* diode ladder, " + std::to_string(stages) + " stages\n";
    s += ".model DMOD D(IS=1e-14 N=1.0)\n";
    s += "Vin in 0 DC 5\n";
    s += "Rin in n0 1k\n";
    for (int i = 0; i < stages; ++i) {
        std::string a = "n" + std::to_string(i);
        std::string b = "n" + std::to_string(i + 1);
        s += "D" + std::to_string(i) + " " + a + " " + b + " DMOD\n";
        s += "Rs" + std::to_string(i) + " " + b + " 0 10k\n";
    }
    s += ".op\n.end\n";
    return s;
}

// ---------------------------------------------------------------------------
// Timing harness
// ---------------------------------------------------------------------------

struct PhaseTiming {
    double parse_ms = 0.0;
    double solve_ms = 0.0;
    int    iterations = 0;
    int    nodes = 0;
    int    vars = 0;
    int    nnz = 0;
    size_t devices = 0;
    bool   converged = false;
    ConvergenceMethod method = ConvergenceMethod::DIRECT;
};

// Run one (parse, solve) cycle and capture per-phase timing + stats.
static PhaseTiming run_once(const std::string& netlist) {
    PhaseTiming pt;
    Simulator sim;

    auto t0 = Clock::now();
    Circuit ckt = sim.parse(netlist);
    auto t1 = Clock::now();
    DCResult res = sim.run_dc(ckt);
    auto t2 = Clock::now();

    pt.parse_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    pt.solve_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    pt.iterations = res.status.iterations;
    pt.converged = res.status.converged;
    pt.method = res.status.convergence_method;
    pt.nodes = ckt.num_nodes();
    pt.vars = ckt.num_vars();
    pt.nnz = ckt.pattern().nnz();
    pt.devices = ckt.devices().size();
    return pt;
}

// Median over `runs` repetitions after `warmup` discarded iterations.
// Parse and solve medians are taken independently (each phase's own median).
static PhaseTiming bench(const std::string& netlist, int warmup, int runs) {
    for (int i = 0; i < warmup; ++i) (void)run_once(netlist);

    std::vector<PhaseTiming> samples;
    samples.reserve(runs);
    for (int i = 0; i < runs; ++i) samples.push_back(run_once(netlist));

    std::vector<double> parse_ms, solve_ms;
    for (auto& s : samples) { parse_ms.push_back(s.parse_ms); solve_ms.push_back(s.solve_ms); }
    std::sort(parse_ms.begin(), parse_ms.end());
    std::sort(solve_ms.begin(), solve_ms.end());

    PhaseTiming out = samples.back();   // copy stats (identical across runs)
    out.parse_ms = parse_ms[runs / 2];
    out.solve_ms = solve_ms[runs / 2];
    return out;
}

// Pick warmup/run counts that keep wall time reasonable as circuits grow.
static std::pair<int,int> scale_iters(int nodes) {
    if (nodes <= 200)   return {3, 21};
    if (nodes <= 2000)  return {2, 11};
    if (nodes <= 8000)  return {1, 5};
    return {1, 3};
}

static void print_header(const char* title) {
    std::printf("\n%s\n", title);
    std::printf("  %8s %8s %8s %9s %5s  %12s %12s %10s\n",
                "nodes", "vars", "nnz", "devices", "iter", "parse(ms)", "solve(ms)", "us/iter");
    std::printf("  %8s %8s %8s %9s %5s  %12s %12s %10s\n",
                "--------", "--------", "--------", "---------", "-----",
                "------------", "------------", "----------");
}

static void print_row(const PhaseTiming& pt) {
    double us_per_iter = pt.iterations > 0 ? (pt.solve_ms * 1000.0 / pt.iterations) : 0.0;
    std::printf("  %8d %8d %8d %9zu %5d  %12.3f %12.3f %10.1f",
                pt.nodes, pt.vars, pt.nnz, pt.devices, pt.iterations,
                pt.parse_ms, pt.solve_ms, us_per_iter);
    if (!pt.converged) std::printf("  [NOT CONVERGED]");
    else if (pt.method != ConvergenceMethod::DIRECT)
        std::printf("  [%s]", convergence_method_name(pt.method));
    std::printf("\n");
}

int main() {
    std::printf("=== bench_solver_throughput ===\n");
    std::printf("Solve-phase throughput on scalable circuits (parse isolated from solve).\n");
    std::printf("Timings are medians over N repetitions (after warmup).\n");

    // ---- Linear: 2D resistor mesh ----
    // Square-ish meshes sized so node count ~= target.
    print_header("LINEAR  -- 2D resistor mesh (stresses sparse LU: symbolic+numeric factor + solve)");
    const std::vector<int> mesh_targets = {100, 1000, 5000, 20000};
    for (int target : mesh_targets) {
        int side = std::max(2, static_cast<int>(std::lround(std::sqrt((double)target))));
        std::string net = make_resistor_mesh(side, side);
        // estimate node count to scale iteration counts
        auto [warmup, runs] = scale_iters(side * side);
        print_row(bench(net, warmup, runs));
    }

    // ---- Nonlinear: diode ladder ----
    print_header("NONLINEAR -- diode+R ladder (stresses Newton iterations + device eval)");
    const std::vector<int> ladder_stages = {100, 1000, 5000, 20000};
    for (int stages : ladder_stages) {
        std::string net = make_diode_ladder(stages);
        auto [warmup, runs] = scale_iters(stages);
        print_row(bench(net, warmup, runs));
    }

    std::printf("\nNotes:\n");
    std::printf("  - parse(ms): Simulator::parse() = netlist build + finalize.\n");
    std::printf("  - solve(ms): Simulator::run_dc() = full Newton DC solve.\n");
    std::printf("  - iter: Newton iterations (DCResult::status.iterations).\n");
    std::printf("  - us/iter: solve time / iteration (per-iteration solver+device cost).\n");
    std::printf("  - Linear mesh -> a small fixed iteration count (load + confirm\n");
    std::printf("    passes); us/iter ~= one full factor+solve cycle.\n");
    std::printf("  - Finer factor/solve/device-eval split needs solver instrumentation\n");
    std::printf("    hooks (not exposed by the public API). See performance-analysis.md.\n");
    return 0;
}
