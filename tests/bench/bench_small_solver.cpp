// Micro-benchmark: SmallSolver (dense LU + AMD) vs BTF per-point timing.
// Measures refactorize+solve at various matrix sizes to find the crossover.

#include "core/small_solver.hpp"
#include "core/btf_solver.hpp"
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
    int32_t n = pat.size();
    mat.clear();
    const auto& entries = pat.entries();
    for (const auto& [r, c] : entries) {
        if (r == c)
            mat.add(pat.offset(r, c), 20.0 + static_cast<double>(r % 5));
        else
            mat.add(pat.offset(r, c), -0.5 - 0.1 * static_cast<double>((r + c) % 3));
    }
    (void)n;
}

struct TimingResult {
    double median_us;
    double min_us;
};

// Benchmark refactorize + solve (real).
template <typename Solver>
static TimingResult bench_refactorize_solve(Solver& solver, NumericMatrix& mat,
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
template <typename Solver>
static TimingResult bench_refactorize_solve_complex(Solver& solver,
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

int main() {
    const int BW = 3;       // half-bandwidth (total bandwidth = 2*BW+1 = 7)
    const int WARMUP = 100;
    const int RUNS = 2000;
    const std::vector<int32_t> sizes = {5, 10, 25, 50, 87, 100, 150, 199, 300, 500};

    std::printf("=== SmallSolver vs BTF Benchmark ===\n");
    std::printf("Matrix: banded sparse (bandwidth=%d, diag-dominant)\n", 2 * BW + 1);
    std::printf("Timing: median of %d refactorize+solve cycles (%d warmup)\n\n", RUNS, WARMUP);

    // ---- Real refactorize + solve ----
    std::printf("Real refactorize + solve:\n");
    std::printf("  %5s %6s  %15s  %10s  %8s\n", "n", "nnz", "SmallSolver(us)", "BTF(us)", "Speedup");
    std::printf("  %5s %6s  %15s  %10s  %8s\n", "-----", "------", "---------------", "----------", "--------");

    for (int32_t n : sizes) {
        auto pat = make_banded(n, BW);
        NumericMatrix mat(pat);
        stamp_diag_dominant(pat, mat);

        std::vector<double> rhs(n);
        for (int32_t i = 0; i < n; ++i) rhs[i] = static_cast<double>(i + 1);

        // SmallSolver
        SmallSolver small;
        small.symbolic(pat);
        small.numeric(pat, mat);
        auto ts = bench_refactorize_solve(small, mat, rhs, WARMUP, RUNS);

        // BTF
        BTFSolver btf;
        btf.symbolic(pat);
        btf.numeric(pat, mat);
        auto tk = bench_refactorize_solve(btf, mat, rhs, WARMUP, RUNS);

        double speedup = tk.median_us / ts.median_us;
        const char* marker = (speedup < 1.05 && speedup > 0.95) ? " ~" :
                             (speedup < 1.0) ? " <-" : "";
        std::printf("  %5d %6d  %13.2f    %8.2f    %6.2fx%s\n",
                    n, pat.nnz(), ts.median_us, tk.median_us, speedup, marker);

        // Verify solutions match
        auto rhs_s = rhs, rhs_b = rhs;
        small.refactorize(mat);
        small.solve(rhs_s);
        btf.refactorize(mat);
        btf.solve(rhs_b);
        double max_diff = 0;
        for (int32_t i = 0; i < n; ++i)
            max_diff = std::max(max_diff, std::abs(rhs_s[i] - rhs_b[i]));
        if (max_diff > 1e-8)
            std::printf("    WARNING: max solution diff = %.2e\n", max_diff);
    }

    // ---- Complex refactorize + solve ----
    std::printf("\nComplex refactorize + solve:\n");
    std::printf("  %5s %6s  %15s  %10s  %8s\n", "n", "nnz", "SmallSolver(us)", "BTF(us)", "Speedup");
    std::printf("  %5s %6s  %15s  %10s  %8s\n", "-----", "------", "---------------", "----------", "--------");

    for (int32_t n : sizes) {
        auto pat = make_banded(n, BW);
        int32_t nnz = pat.nnz();

        // Build complex values: interleaved [real, imag] per entry
        std::vector<double> ax(2 * nnz, 0.0);
        const auto& entries = pat.entries();
        for (int32_t idx = 0; idx < nnz; ++idx) {
            auto [r, c] = entries[idx];
            if (r == c) {
                ax[2 * idx]     = 20.0 + static_cast<double>(r % 5);
                ax[2 * idx + 1] = 2.0 * 3.14159265 * 1e3;  // imaginary from freq
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

        SmallSolver small;
        small.symbolic(pat);
        small.numeric_complex(pat, ax);
        auto ts = bench_refactorize_solve_complex(small, ax, rhs, WARMUP, RUNS);

        BTFSolver btf;
        btf.symbolic(pat);
        btf.numeric_complex(pat, ax);
        auto tk = bench_refactorize_solve_complex(btf, ax, rhs, WARMUP, RUNS);

        double speedup = tk.median_us / ts.median_us;
        const char* marker = (speedup < 1.05 && speedup > 0.95) ? " ~" :
                             (speedup < 1.0) ? " <-" : "";
        std::printf("  %5d %6d  %13.2f    %8.2f    %6.2fx%s\n",
                    n, nnz, ts.median_us, tk.median_us, speedup, marker);

        // Verify solutions match
        auto rhs_s = rhs, rhs_b = rhs;
        small.refactorize_complex(ax);
        small.solve_complex(rhs_s);
        btf.refactorize_complex(ax);
        btf.solve_complex(rhs_b);
        double max_diff = 0;
        for (int32_t i = 0; i < n; ++i) {
            double dr = rhs_s[2 * i] - rhs_b[2 * i];
            double di = rhs_s[2 * i + 1] - rhs_b[2 * i + 1];
            max_diff = std::max(max_diff, std::hypot(dr, di));
        }
        if (max_diff > 1e-8)
            std::printf("    WARNING: max complex solution diff = %.2e\n", max_diff);
    }

    return 0;
}
