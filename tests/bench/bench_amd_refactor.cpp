// bench_amd_refactor — isolate the Stage-3 refactor-REUSE gain in AmdLuSolver.
//
// We build a fixed sparsity pattern (2-D mesh -> nontrivial fill, like a real
// MNA matrix) and measure, on the SAME pattern:
//   FULL   : numeric()      — from-scratch Gilbert-Peierls (DFS + pivot search)
//   REUSE  : refactorize()  — KLU-style replay (no DFS, no pivot search)
// Both recompute numeric values for a changed matrix; only REUSE reuses the
// stored structure + pivot order. Reported ratio = per-solve speedup from reuse.

#include "core/amd_lu_solver.hpp"
#include "core/matrix.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace neospice;
using Clock = std::chrono::high_resolution_clock;

// 2-D 5-point mesh on a side x side grid -> n = side*side unknowns.
static SparsityPattern make_mesh_pattern(int side) {
    int n = side * side;
    SparsityBuilder sb(n);
    auto idx = [side](int x, int y) { return y * side + x; };
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            int i = idx(x, y);
            sb.add(i, i);
            if (x > 0)        { sb.add(i, idx(x - 1, y)); }
            if (x < side - 1) { sb.add(i, idx(x + 1, y)); }
            if (y > 0)        { sb.add(i, idx(x, y - 1)); }
            if (y < side - 1) { sb.add(i, idx(x, y + 1)); }
        }
    return sb.build();
}

// Fill a diagonally-dominant matrix (stable pivots) scaled by s.
static void fill_mesh(const SparsityPattern& pat, NumericMatrix& m, double s) {
    int n = pat.size();
    std::vector<double> off(n, 0.0);
    for (auto& [r, c] : pat.entries()) {
        if (r == c) continue;
        double v = -1.0 * s * (1.0 + 0.01 * ((r + c) % 7));
        m.add(pat.offset(r, c), v);
        off[r] += std::fabs(v);
    }
    for (int i = 0; i < n; ++i) m.add(pat.offset(i, i), off[i] + 4.0 * s);
}

static double median(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

int main() {
    std::printf("=== bench_amd_refactor (Stage-3 reuse gain) ===\n");
    std::printf("Per-call factor time: FULL numeric() vs REUSE refactorize().\n");
    std::printf("  %6s %8s  %12s %12s %9s\n",
                "side", "n", "full(us)", "reuse(us)", "speedup");
    std::printf("  %6s %8s  %12s %12s %9s\n",
                "------", "--------", "------------", "------------", "---------");

    for (int side : {16, 32, 48, 64, 96}) {
        SparsityPattern pat = make_mesh_pattern(side);
        int n = pat.size();

        // Pre-build a set of value variants (simulate changing matrix values).
        const int variants = 8;
        std::vector<NumericMatrix> mats;
        mats.reserve(variants);
        for (int v = 0; v < variants; ++v) {
            mats.emplace_back(pat);
            fill_mesh(pat, mats.back(), 1.0 + 0.13 * v);
        }

        AmdLuSolver solver;
        solver.symbolic(pat);
        // Prime: one full factor so refactorize has replay data.
        solver.numeric(pat, mats[0]);

        const int warmup = 3;
        const int reps = (n <= 1024) ? 200 : (n <= 4096 ? 60 : 20);

        // --- REUSE: refactorize() (fast replay path) ---
        for (int w = 0; w < warmup; ++w) solver.refactorize(mats[w % variants]);
        int64_t fast_before = solver.refactor_fast_count();
        std::vector<double> reuse_us;
        reuse_us.reserve(reps);
        for (int r = 0; r < reps; ++r) {
            const NumericMatrix& m = mats[r % variants];
            auto t0 = Clock::now();
            solver.refactorize(m);
            auto t1 = Clock::now();
            reuse_us.push_back(
                std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        int64_t fast_taken = solver.refactor_fast_count() - fast_before;

        // --- FULL: numeric() (from-scratch factor) ---
        for (int w = 0; w < warmup; ++w) solver.numeric(pat, mats[w % variants]);
        std::vector<double> full_us;
        full_us.reserve(reps);
        for (int r = 0; r < reps; ++r) {
            const NumericMatrix& m = mats[r % variants];
            auto t0 = Clock::now();
            solver.numeric(pat, m);
            auto t1 = Clock::now();
            full_us.push_back(
                std::chrono::duration<double, std::micro>(t1 - t0).count());
        }

        double f = median(full_us), ru = median(reuse_us);
        std::printf("  %6d %8d  %12.2f %12.2f %8.2fx  (%lld/%d fast)\n",
                    side, n, f, ru, f / ru,
                    (long long)fast_taken, reps);
    }
    return 0;
}
