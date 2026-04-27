#pragma once
#include "core/linear_solver.hpp"
#include "core/small_solver.hpp"
#include "core/btf.hpp"
#include <memory>
#include <vector>

namespace neospice {

class BTFSolver : public LinearSolver {
public:
    BTFSolver();
    ~BTFSolver() override = default;

    void symbolic(const SparsityPattern& pattern) override;
    void numeric(const SparsityPattern& pattern, const NumericMatrix& mat) override;
    void refactorize(const NumericMatrix& mat) override;
    void solve(std::vector<double>& rhs) override;
    void numeric_complex(const SparsityPattern& pattern,
                         const std::vector<double>& ax) override;
    void refactorize_complex(const std::vector<double>& ax) override;
    void solve_complex(std::vector<double>& rhs) override;

private:
    int32_t n_ = 0;
    BTFResult btf_;

    struct BlockData {
        int32_t size;
        SparsityPattern pattern;
        std::unique_ptr<SmallSolver> solver;
        std::vector<int32_t> val_map;  // block CSC index -> original matrix CSC index
    };
    std::vector<BlockData> blocks_;

    struct Coupling {
        int32_t block_row;     // local row within this block
        int32_t source_index;  // permuted index of the source (in already-solved block)
        int32_t orig_offset;   // offset into original NumericMatrix values
    };
    std::vector<std::vector<Coupling>> couplings_;  // per block

    // Store original values for coupling during solve
    std::vector<double> orig_values_;
    std::vector<double> orig_values_z_;  // complex interleaved

    bool symbolized_ = false;
    bool factored_ = false;
    bool factored_z_ = false;
};

}  // namespace neospice
