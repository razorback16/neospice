#include "core/btf_solver.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>

namespace neospice {

BTFSolver::BTFSolver() = default;

void BTFSolver::symbolic(const SparsityPattern& pattern) {
    n_ = pattern.size();
    CSCData csc = pattern.to_csc();

    // Run BTF decomposition
    btf_ = btf_decompose(n_, csc.col_ptr.data(), csc.row_idx.data());

    blocks_.clear();
    blocks_.reserve(btf_.nblocks);
    couplings_.clear();
    couplings_.resize(btf_.nblocks);

    // For each block, extract sub-matrix sparsity and build val_map
    for (int32_t b = 0; b < btf_.nblocks; ++b) {
        int32_t bstart = btf_.block_ptr[b];
        int32_t bend = btf_.block_ptr[b + 1];
        int32_t bsize = bend - bstart;

        SparsityBuilder sb(bsize);
        // Temporary storage to track val_map entries in CSC order
        std::vector<std::pair<std::pair<int32_t,int32_t>, int32_t>> block_entries;
        // (local_col, local_row) -> original CSC index

        // Iterate over all original CSC entries
        for (int32_t orig_col = 0; orig_col < n_; ++orig_col) {
            int32_t perm_col = btf_.inv_perm[orig_col];
            for (int32_t k = csc.col_ptr[orig_col]; k < csc.col_ptr[orig_col + 1]; ++k) {
                int32_t orig_row = csc.row_idx[k];
                int32_t perm_row = btf_.inv_perm[orig_row];

                if (perm_col >= bstart && perm_col < bend &&
                    perm_row >= bstart && perm_row < bend) {
                    // Diagonal block entry
                    int32_t local_row = perm_row - bstart;
                    int32_t local_col = perm_col - bstart;
                    sb.add(local_row, local_col);
                    block_entries.push_back({{local_col, local_row}, k});
                } else if (perm_row >= bstart && perm_row < bend &&
                           perm_col < bstart) {
                    // Coupling: row in block b, col in an earlier block (already solved)
                    Coupling c;
                    c.block_row = perm_row - bstart;
                    c.source_index = perm_col;  // permuted index in the full system
                    c.orig_offset = k;          // offset into original values
                    couplings_[b].push_back(c);
                }
            }
        }

        auto block_pat = sb.build();

        // Build val_map: for each entry in the block's CSC (sorted by col,row),
        // record the original CSC index
        // The block pattern entries are sorted by (col, row) after build().
        // We need to match them with block_entries.
        std::sort(block_entries.begin(), block_entries.end());

        std::vector<int32_t> val_map;
        val_map.reserve(block_pat.nnz());
        const auto& pat_entries = block_pat.entries();
        size_t be_idx = 0;
        for (int32_t i = 0; i < block_pat.nnz(); ++i) {
            auto [row, col] = pat_entries[i];
            // block_entries sorted by (col, row), pat_entries sorted by (col, row) -- but
            // block_entries uses {col, row} as key while pat_entries is {row, col}
            // Need to find the matching entry
            std::pair<int32_t,int32_t> key = {col, row};
            // Linear scan through sorted block_entries
            while (be_idx < block_entries.size() && block_entries[be_idx].first < key)
                ++be_idx;
            if (be_idx < block_entries.size() && block_entries[be_idx].first == key) {
                val_map.push_back(block_entries[be_idx].second);
                ++be_idx;
            }
        }

        auto solver = std::make_unique<SmallSolver>();
        solver->symbolic(block_pat);

        blocks_.push_back(BlockData{
            bsize,
            std::move(block_pat),
            std::move(solver),
            std::move(val_map)
        });
    }

    symbolized_ = true;
    factored_ = false;
    factored_z_ = false;
}

void BTFSolver::numeric(const SparsityPattern& pattern, const NumericMatrix& mat) {
    if (!symbolized_)
        throw std::logic_error("BTFSolver::numeric: symbolic() not called");

    // Store original values for coupling during solve
    orig_values_.assign(mat.data(), mat.data() + mat.nnz());

    for (int32_t b = 0; b < btf_.nblocks; ++b) {
        auto& blk = blocks_[b];
        NumericMatrix block_mat(blk.pattern);
        for (int32_t i = 0; i < static_cast<int32_t>(blk.val_map.size()); ++i) {
            block_mat.add(MatrixOffset(i), mat.data()[blk.val_map[i]]);
        }
        blk.solver->numeric(blk.pattern, block_mat);
    }

    factored_ = true;
}

void BTFSolver::refactorize(const NumericMatrix& mat) {
    if (!symbolized_)
        throw std::logic_error("BTFSolver::refactorize: symbolic() not called");
    if (!factored_)
        throw std::logic_error("BTFSolver::refactorize: numeric() not called");

    // Update stored values
    orig_values_.assign(mat.data(), mat.data() + mat.nnz());

    for (int32_t b = 0; b < btf_.nblocks; ++b) {
        auto& blk = blocks_[b];
        NumericMatrix block_mat(blk.pattern);
        for (int32_t i = 0; i < static_cast<int32_t>(blk.val_map.size()); ++i) {
            block_mat.add(MatrixOffset(i), mat.data()[blk.val_map[i]]);
        }
        blk.solver->refactorize(block_mat);
    }
}

void BTFSolver::solve(std::vector<double>& rhs) {
    if (!symbolized_)
        throw std::logic_error("BTFSolver::solve: symbolic() not called");
    if (!factored_)
        throw std::logic_error("BTFSolver::solve: numeric() not called");

    // Permute rhs into permuted order
    std::vector<double> perm_rhs(n_);
    for (int32_t i = 0; i < n_; ++i)
        perm_rhs[i] = rhs[btf_.perm[i]];

    // Process blocks from first to last (forward substitution through lower-triangular BTF)
    for (int32_t b = 0; b < btf_.nblocks; ++b) {
        int32_t bstart = btf_.block_ptr[b];
        int32_t bsize = blocks_[b].size;

        // Apply coupling contributions from already-solved blocks
        for (const auto& c : couplings_[b]) {
            double val = orig_values_[c.orig_offset];
            perm_rhs[bstart + c.block_row] -= val * perm_rhs[c.source_index];
        }

        // Extract block portion, solve, write back
        std::vector<double> block_rhs(bsize);
        for (int32_t i = 0; i < bsize; ++i)
            block_rhs[i] = perm_rhs[bstart + i];

        blocks_[b].solver->solve(block_rhs);

        for (int32_t i = 0; i < bsize; ++i)
            perm_rhs[bstart + i] = block_rhs[i];
    }

    // Unpermute: rhs[perm[i]] = perm_rhs[i]
    for (int32_t i = 0; i < n_; ++i)
        rhs[btf_.perm[i]] = perm_rhs[i];
}

void BTFSolver::numeric_complex(const SparsityPattern& pattern,
                                const std::vector<double>& ax) {
    if (!symbolized_)
        throw std::logic_error("BTFSolver::numeric_complex: symbolic() not called");

    // Store original complex values for coupling during solve
    orig_values_z_ = ax;

    for (int32_t b = 0; b < btf_.nblocks; ++b) {
        auto& blk = blocks_[b];
        // Build complex block values: 2 doubles per entry, interleaved
        std::vector<double> block_ax(2 * blk.val_map.size());
        for (int32_t i = 0; i < static_cast<int32_t>(blk.val_map.size()); ++i) {
            block_ax[2 * i]     = ax[2 * blk.val_map[i]];
            block_ax[2 * i + 1] = ax[2 * blk.val_map[i] + 1];
        }
        blk.solver->numeric_complex(blk.pattern, block_ax);
    }

    factored_z_ = true;
}

void BTFSolver::refactorize_complex(const std::vector<double>& ax) {
    if (!symbolized_)
        throw std::logic_error("BTFSolver::refactorize_complex: symbolic() not called");
    if (!factored_z_)
        throw std::logic_error("BTFSolver::refactorize_complex: numeric_complex() not called");

    orig_values_z_ = ax;

    for (int32_t b = 0; b < btf_.nblocks; ++b) {
        auto& blk = blocks_[b];
        std::vector<double> block_ax(2 * blk.val_map.size());
        for (int32_t i = 0; i < static_cast<int32_t>(blk.val_map.size()); ++i) {
            block_ax[2 * i]     = ax[2 * blk.val_map[i]];
            block_ax[2 * i + 1] = ax[2 * blk.val_map[i] + 1];
        }
        blk.solver->refactorize_complex(block_ax);
    }
}

void BTFSolver::solve_complex(std::vector<double>& rhs) {
    if (!symbolized_)
        throw std::logic_error("BTFSolver::solve_complex: symbolic() not called");
    if (!factored_z_)
        throw std::logic_error("BTFSolver::solve_complex: numeric_complex() not called");

    // Permute rhs (interleaved real,imag)
    std::vector<double> perm_rhs(2 * n_);
    for (int32_t i = 0; i < n_; ++i) {
        perm_rhs[2 * i]     = rhs[2 * btf_.perm[i]];
        perm_rhs[2 * i + 1] = rhs[2 * btf_.perm[i] + 1];
    }

    // Process blocks from first to last (forward substitution through lower-triangular BTF)
    for (int32_t b = 0; b < btf_.nblocks; ++b) {
        int32_t bstart = btf_.block_ptr[b];
        int32_t bsize = blocks_[b].size;

        // Apply coupling contributions (complex multiply for subtraction)
        for (const auto& c : couplings_[b]) {
            double val_re = orig_values_z_[2 * c.orig_offset];
            double val_im = orig_values_z_[2 * c.orig_offset + 1];
            double src_re = perm_rhs[2 * c.source_index];
            double src_im = perm_rhs[2 * c.source_index + 1];
            // Subtract (val_re + val_im*i) * (src_re + src_im*i)
            perm_rhs[2 * (bstart + c.block_row)]     -= val_re * src_re - val_im * src_im;
            perm_rhs[2 * (bstart + c.block_row) + 1] -= val_re * src_im + val_im * src_re;
        }

        // Extract block portion, solve, write back
        std::vector<double> block_rhs(2 * bsize);
        for (int32_t i = 0; i < bsize; ++i) {
            block_rhs[2 * i]     = perm_rhs[2 * (bstart + i)];
            block_rhs[2 * i + 1] = perm_rhs[2 * (bstart + i) + 1];
        }

        blocks_[b].solver->solve_complex(block_rhs);

        for (int32_t i = 0; i < bsize; ++i) {
            perm_rhs[2 * (bstart + i)]     = block_rhs[2 * i];
            perm_rhs[2 * (bstart + i) + 1] = block_rhs[2 * i + 1];
        }
    }

    // Unpermute
    for (int32_t i = 0; i < n_; ++i) {
        rhs[2 * btf_.perm[i]]     = perm_rhs[2 * i];
        rhs[2 * btf_.perm[i] + 1] = perm_rhs[2 * i + 1];
    }
}

}  // namespace neospice
