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

// Build a block-diagonal matrix with sparse coupling between blocks.
// Models real circuit topology: nblocks dense subcircuits connected
// by a few inter-block wires (like SPICE subcircuit instances).
static SparsityPattern make_block_coupled(int32_t block_size, int32_t nblocks,
                                           int32_t coupling_per_block) {
    int32_t n = block_size * nblocks;
    SparsityBuilder sb(n);
    for (int32_t b = 0; b < nblocks; ++b) {
        int32_t base = b * block_size;
        // Dense block (like a subcircuit's internal MNA)
        for (int32_t i = 0; i < block_size; ++i)
            for (int32_t j = 0; j < block_size; ++j)
                sb.add(base + i, base + j);
        // Sparse coupling to next block
        if (b + 1 < nblocks) {
            int32_t next = (b + 1) * block_size;
            for (int32_t k = 0; k < coupling_per_block && k < block_size; ++k) {
                sb.add(base + k, next + k);
                sb.add(next + k, base + k);
            }
        }
    }
    return sb.build();
}

// Build block-diagonal with UNIDIRECTIONAL forward coupling (cascade topology).
// BTF can decompose this into separate blocks.
static SparsityPattern make_block_cascade(int32_t block_size, int32_t nblocks,
                                           int32_t coupling_per_block) {
    int32_t n = block_size * nblocks;
    SparsityBuilder sb(n);
    for (int32_t b = 0; b < nblocks; ++b) {
        int32_t base = b * block_size;
        for (int32_t i = 0; i < block_size; ++i)
            for (int32_t j = 0; j < block_size; ++j)
                sb.add(base + i, base + j);
        if (b + 1 < nblocks) {
            int32_t next = (b + 1) * block_size;
            for (int32_t k = 0; k < coupling_per_block && k < block_size; ++k)
                sb.add(next + k, base + k);  // forward only: block b feeds block b+1
        }
    }
    return sb.build();
}

static void stamp_block_coupled(const SparsityPattern& pat, NumericMatrix& mat,
                                 int32_t block_size) {
    mat.clear();
    const auto& entries = pat.entries();
    for (const auto& [r, c] : entries) {
        int32_t br = r / block_size, bc = c / block_size;
        if (br == bc) {
            if (r == c)
                mat.add(pat.offset(r, c), 10.0 + static_cast<double>(r % 7));
            else
                mat.add(pat.offset(r, c), -0.3 - 0.1 * static_cast<double>((r + c) % 4));
        } else {
            mat.add(pat.offset(r, c), -0.1);
        }
    }
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

    std::printf("=== SmallSolver vs BTF Benchmark ===\n");
    std::printf("Matrix: banded sparse (bandwidth=%d, diag-dominant)\n", 2 * BW + 1);
    std::printf("Timing: median of refactorize+solve cycles (iterations scale with size)\n\n");

    // ---- Real refactorize + solve ----
    std::printf("Real refactorize + solve:\n");
    std::printf("  %6s %7s %5s  %15s  %10s  %8s\n", "n", "nnz", "runs", "SmallSolver(us)", "BTF(us)", "Speedup");
    std::printf("  %6s %7s %5s  %15s  %10s  %8s\n", "------", "-------", "-----", "---------------", "----------", "--------");

    for (int32_t n : sizes) {
        auto [warmup, runs] = scale_iters(n);
        auto pat = make_banded(n, BW);
        NumericMatrix mat(pat);
        stamp_diag_dominant(pat, mat);

        std::vector<double> rhs(n);
        for (int32_t i = 0; i < n; ++i) rhs[i] = static_cast<double>(i + 1);

        SmallSolver small;
        small.symbolic(pat);
        small.numeric(pat, mat);
        auto ts = bench_refactorize_solve(small, mat, rhs, warmup, runs);

        BTFSolver btf;
        btf.symbolic(pat);
        btf.numeric(pat, mat);
        auto tk = bench_refactorize_solve(btf, mat, rhs, warmup, runs);

        double speedup = tk.median_us / ts.median_us;
        const char* marker = (speedup < 1.05 && speedup > 0.95) ? " ~" :
                             (speedup < 1.0) ? " <-" : "";
        std::printf("  %6d %7d %5d  %13.2f    %8.2f    %6.2fx%s\n",
                    n, pat.nnz(), runs, ts.median_us, tk.median_us, speedup, marker);

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
    std::printf("  %6s %7s %5s  %15s  %10s  %8s\n", "n", "nnz", "runs", "SmallSolver(us)", "BTF(us)", "Speedup");
    std::printf("  %6s %7s %5s  %15s  %10s  %8s\n", "------", "-------", "-----", "---------------", "----------", "--------");

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

        SmallSolver small;
        small.symbolic(pat);
        small.numeric_complex(pat, ax);
        auto ts = bench_refactorize_solve_complex(small, ax, rhs, warmup, runs);

        BTFSolver btf;
        btf.symbolic(pat);
        btf.numeric_complex(pat, ax);
        auto tk = bench_refactorize_solve_complex(btf, ax, rhs, warmup, runs);

        double speedup = tk.median_us / ts.median_us;
        const char* marker = (speedup < 1.05 && speedup > 0.95) ? " ~" :
                             (speedup < 1.0) ? " <-" : "";
        std::printf("  %6d %7d %5d  %13.2f    %8.2f    %6.2fx%s\n",
                    n, nnz, runs, ts.median_us, tk.median_us, speedup, marker);

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

    // ---- Block-structured (circuit-like) benchmark ----
    std::printf("\n=== Block-Structured (Circuit-Like) Benchmark ===\n");
    std::printf("Matrix: nblocks dense subcircuits with sparse inter-block coupling\n");
    std::printf("This models real SPICE circuits where BTF decomposition helps.\n\n");

    struct BlockConfig { int32_t block_size; int32_t nblocks; int32_t coupling; };
    const std::vector<BlockConfig> configs = {
        {10, 5,  2},   // 50 nodes, 5 blocks of 10
        {10, 10, 2},   // 100 nodes
        {15, 10, 3},   // 150 nodes
        {20, 10, 3},   // 200 nodes
        {20, 25, 3},   // 500 nodes
        {25, 40, 3},   // 1000 nodes
        {20, 50, 3},   // 1000 nodes (more smaller blocks)
        {25, 80, 3},   // 2000 nodes
        {50, 100, 3},  // 5000 nodes
        {50, 200, 3},  // 10000 nodes
    };

    std::printf("Real refactorize + solve (block-structured):\n");
    std::printf("  %6s %3s %7s %5s  %15s  %10s  %8s\n",
                "n", "blk", "nnz", "runs", "SmallSolver(us)", "BTF(us)", "Speedup");
    std::printf("  %6s %3s %7s %5s  %15s  %10s  %8s\n",
                "------", "---", "-------", "-----", "---------------", "----------", "--------");

    for (const auto& cfg : configs) {
        int32_t n = cfg.block_size * cfg.nblocks;
        auto [warmup, runs] = scale_iters(n);
        auto pat = make_block_coupled(cfg.block_size, cfg.nblocks, cfg.coupling);
        NumericMatrix mat(pat);
        stamp_block_coupled(pat, mat, cfg.block_size);

        std::vector<double> rhs(n);
        for (int32_t i = 0; i < n; ++i) rhs[i] = static_cast<double>(i + 1);

        SmallSolver small;
        small.symbolic(pat);
        small.numeric(pat, mat);
        auto ts = bench_refactorize_solve(small, mat, rhs, warmup, runs);

        BTFSolver btf;
        btf.symbolic(pat);
        btf.numeric(pat, mat);
        auto tk = bench_refactorize_solve(btf, mat, rhs, warmup, runs);

        double speedup = tk.median_us / ts.median_us;
        const char* marker = (speedup < 1.05 && speedup > 0.95) ? " ~" :
                             (speedup < 1.0) ? " <-BTF wins" : "";
        std::printf("  %6d %3d %7d %5d  %13.2f    %8.2f    %6.2fx%s\n",
                    n, cfg.nblocks, pat.nnz(), runs, ts.median_us, tk.median_us, speedup, marker);

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

    // ---- Cascade (unidirectional) block benchmark ----
    std::printf("\nCascade topology (unidirectional coupling — BTF can decompose):\n");
    std::printf("  %6s %3s %7s %5s  %15s  %10s  %8s\n",
                "n", "blk", "nnz", "runs", "SmallSolver(us)", "BTF(us)", "Speedup");
    std::printf("  %6s %3s %7s %5s  %15s  %10s  %8s\n",
                "------", "---", "-------", "-----", "---------------", "----------", "--------");

    for (const auto& cfg : configs) {
        int32_t n = cfg.block_size * cfg.nblocks;
        auto [warmup, runs] = scale_iters(n);
        auto pat = make_block_cascade(cfg.block_size, cfg.nblocks, cfg.coupling);
        NumericMatrix mat(pat);
        stamp_block_coupled(pat, mat, cfg.block_size);

        std::vector<double> rhs(n);
        for (int32_t i = 0; i < n; ++i) rhs[i] = static_cast<double>(i + 1);

        SmallSolver small;
        small.symbolic(pat);
        small.numeric(pat, mat);
        auto ts = bench_refactorize_solve(small, mat, rhs, warmup, runs);

        BTFSolver btf;
        btf.symbolic(pat);
        btf.numeric(pat, mat);
        auto tk = bench_refactorize_solve(btf, mat, rhs, warmup, runs);

        double speedup = tk.median_us / ts.median_us;
        const char* marker = (speedup < 1.05 && speedup > 0.95) ? " ~" :
                             (speedup < 1.0) ? " <-BTF wins" : "";
        std::printf("  %6d %3d %7d %5d  %13.2f    %8.2f    %6.2fx%s\n",
                    n, cfg.nblocks, pat.nnz(), runs, ts.median_us, tk.median_us, speedup, marker);

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

    return 0;
}
