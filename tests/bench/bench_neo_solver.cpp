// Micro-benchmark: NeoSolver (dense LU + AMD sparse column-LU) per-point timing.
// Measures refactorize+solve at various matrix sizes.

#include "core/neo_solver.hpp"
#include "core/matrix.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <vector>

using namespace neospice;
using Clock = std::chrono::high_resolution_clock;

// Build a banded sparse matrix: diagonal + bandwidth off-diagonals.
static SparsityPattern make_banded(int32_t n, int32_t bw) {
    SparsityBuilder sb(n);
    for (int32_t j = 0; j < n; ++j) {
        for (int32_t di = -bw; di <= bw; ++di) {
            int32_t i = j + di;
            if (i >= 0 && i < n) sb.add(i, j);
        }
    }
    return sb.build();
}

// Stamp diagonally-dominant values into a NumericMatrix.
static void stamp_diag_dominant(const SparsityPattern& pat, NumericMatrix& mat) {
    mat.clear();
    const auto& entries = pat.entries();
    for (const auto& [r, c] : entries) {
        if (r == c)
            mat.add(pat.offset(r, c), 20.0 + static_cast<double>(r % 5));
        else
            mat.add(pat.offset(r, c), -0.5 - 0.1 * static_cast<double>((r + c) % 3));
    }
}

struct TimingResult {
    double median_us;
    double min_us;
};

// Benchmark refactorize + solve (real).
static TimingResult bench_refactorize_solve(NeoSolver& solver, NumericMatrix& mat,
                                            std::vector<double>& rhs_template,
                                            int warmup, int runs) {
    for (int i = 0; i < warmup; ++i) {
        solver.refactorize(mat);
        auto rhs = rhs_template;
        solver.solve(rhs);
    }

    std::vector<double> samples(runs);
    for (int i = 0; i < runs; ++i) {
        auto t0 = Clock::now();
        solver.refactorize(mat);
        auto rhs = rhs_template;
        solver.solve(rhs);
        auto t1 = Clock::now();
        samples[i] = std::chrono::duration<double, std::micro>(t1 - t0).count();
    }
    std::sort(samples.begin(), samples.end());
    return {samples[runs / 2], samples.front()};
}

// Benchmark refactorize + solve (complex).
static TimingResult bench_refactorize_solve_complex(NeoSolver& solver,
                                                    const std::vector<double>& ax,
                                                    std::vector<double>& rhs_template,
                                                    int warmup, int runs) {
    for (int i = 0; i < warmup; ++i) {
        solver.refactorize_complex(ax);
        auto rhs = rhs_template;
        solver.solve_complex(rhs);
    }

    std::vector<double> samples(runs);
    for (int i = 0; i < runs; ++i) {
        auto t0 = Clock::now();
        solver.refactorize_complex(ax);
        auto rhs = rhs_template;
        solver.solve_complex(rhs);
        auto t1 = Clock::now();
        samples[i] = std::chrono::duration<double, std::micro>(t1 - t0).count();
    }
    std::sort(samples.begin(), samples.end());
    return {samples[runs / 2], samples.front()};
}

// Scale iterations down for large matrices so the benchmark finishes in reasonable time.
static std::pair<int,int> scale_iters(int32_t n) {
    if (n <= 500) return {100, 2000};
    if (n <= 2000) return {20, 500};
    if (n <= 5000) return {5, 100};
    return {3, 30};
}

int main() {
    const int BW = 3;       // half-bandwidth (total bandwidth = 2*BW+1 = 7)
    const std::vector<int32_t> sizes = {5, 10, 25, 50, 87, 100, 150, 199, 300, 500, 1000, 2000, 5000, 10000};

    std::printf("=== NeoSolver Benchmark ===\n");
    std::printf("Matrix: banded sparse (bandwidth=%d, diag-dominant)\n", 2 * BW + 1);
    std::printf("Timing: median of refactorize+solve cycles (iterations scale with size)\n\n");

    // ---- Real refactorize + solve ----
    std::printf("Real refactorize + solve:\n");
    std::printf("  %6s %7s %5s  %15s  %10s\n", "n", "nnz", "runs", "NeoSolver(us)", "min(us)");
    std::printf("  %6s %7s %5s  %15s  %10s\n", "------", "-------", "-----", "---------------", "----------");

    for (int32_t n : sizes) {
        auto [warmup, runs] = scale_iters(n);
        auto pat = make_banded(n, BW);
        NumericMatrix mat(pat);
        stamp_diag_dominant(pat, mat);

        std::vector<double> rhs(n);
        for (int32_t i = 0; i < n; ++i) rhs[i] = static_cast<double>(i + 1);

        NeoSolver solver;
        solver.symbolic(pat);
        solver.numeric(pat, mat);
        auto ts = bench_refactorize_solve(solver, mat, rhs, warmup, runs);

        std::printf("  %6d %7d %5d  %13.2f    %8.2f\n",
                    n, pat.nnz(), runs, ts.median_us, ts.min_us);
    }

    // ---- Complex refactorize + solve ----
    std::printf("\nComplex refactorize + solve:\n");
    std::printf("  %6s %7s %5s  %15s  %10s\n", "n", "nnz", "runs", "NeoSolver(us)", "min(us)");
    std::printf("  %6s %7s %5s  %15s  %10s\n", "------", "-------", "-----", "---------------", "----------");

    for (int32_t n : sizes) {
        auto [warmup, runs] = scale_iters(n);
        auto pat = make_banded(n, BW);
        int32_t nnz = pat.nnz();

        std::vector<double> ax(2 * nnz, 0.0);
        const auto& entries = pat.entries();
        for (int32_t idx = 0; idx < nnz; ++idx) {
            auto [r, c] = entries[idx];
            if (r == c) {
                ax[2 * idx]     = 20.0 + static_cast<double>(r % 5);
                ax[2 * idx + 1] = 2.0 * 3.14159265 * 1e3;
            } else {
                ax[2 * idx]     = -0.5;
                ax[2 * idx + 1] = -0.1;
            }
        }

        std::vector<double> rhs(2 * n);
        for (int32_t i = 0; i < n; ++i) {
            rhs[2 * i]     = static_cast<double>(i + 1);
            rhs[2 * i + 1] = 0.5;
        }

        NeoSolver solver;
        solver.symbolic(pat);
        solver.numeric_complex(pat, ax);
        auto ts = bench_refactorize_solve_complex(solver, ax, rhs, warmup, runs);

        std::printf("  %6d %7d %5d  %13.2f    %8.2f\n",
                    n, nnz, runs, ts.median_us, ts.min_us);
    }

    return 0;
}
