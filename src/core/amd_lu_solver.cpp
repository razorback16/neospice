#include "core/amd_lu_solver.hpp"
#include "core/amd.hpp"
#include <cmath>
#include <stdexcept>

namespace neospice {

AmdLuSolver::AmdLuSolver() : complex_solver_(std::make_unique<NeoSolver>()) {}
AmdLuSolver::~AmdLuSolver() = default;

void AmdLuSolver::symbolic(const SparsityPattern& pattern) {
    n_ = pattern.size();
    CSCData csc = pattern.to_csc();
    nnz_ = static_cast<int32_t>(csc.row_idx.size());

    a_colptr_ = csc.col_ptr;
    a_rowidx_ = csc.row_idx;

    // --- Build structurally-symmetric pattern (A | A^T) for AMD ---
    // amd_ordering already symmetrizes internally, but it expects a valid CSC.
    // We can pass A directly (it symmetrizes by mirroring edges).
    q_ = amd_ordering(n_, a_colptr_.data(), a_rowidx_.data());
    if (static_cast<int32_t>(q_.size()) != n_) {
        // Fallback to natural ordering (e.g. n_==0).
        q_.resize(n_);
        for (int32_t i = 0; i < n_; ++i) q_[i] = i;
    }

    factored_ = false;
    symbolized_ = true;

    pinv_.assign(n_, -1);
    x_.assign(n_, 0.0);

    // Keep the complex solver's symbolic state in sync for delegation.
    complex_solver_->symbolic(pattern);
}

// Depth-first search over the graph of L (pattern of columns already computed),
// starting at node j. Marks reachable nodes and pushes them onto xi in
// topological order. Mirrors CSparse cs_dfs but uses an explicit visited array
// instead of sign-flipping the column pointers.
static int32_t lu_dfs(int32_t j, const int32_t* Lp, const int32_t* Li,
                      const int32_t* pinv, int32_t top, int32_t* xi,
                      int32_t* pstack, std::vector<char>& visited) {
    int32_t head = 0;
    xi[0] = j;
    while (head >= 0) {
        j = xi[head];
        int32_t jnew = pinv[j];  // column of L for original row j, or -1
        if (!visited[j]) {
            visited[j] = 1;
            pstack[head] = (jnew < 0) ? 0 : Lp[jnew];
        }
        int32_t done = 1;
        int32_t p2 = (jnew < 0) ? 0 : Lp[jnew + 1];
        for (int32_t p = pstack[head]; p < p2; ++p) {
            int32_t i = Li[p];          // neighbor (original row index)
            if (visited[i]) continue;
            pstack[head] = p;           // pause dfs of node j
            xi[++head] = i;             // start dfs at node i
            done = 0;
            break;
        }
        if (done) {
            --head;
            xi[--top] = j;
        }
    }
    return top;
}

bool AmdLuSolver::factor_(const NumericMatrix& mat, double diag_gmin) {
    const double* values = mat.data();
    const int32_t n = n_;

    Lp_.assign(n + 1, 0);
    Up_.assign(n + 1, 0);
    Li_.clear();
    Lx_.clear();
    Ui_.clear();
    Ux_.clear();
    // Heuristic initial reservation.
    Li_.reserve(static_cast<size_t>(nnz_) * 4 + n);
    Lx_.reserve(static_cast<size_t>(nnz_) * 4 + n);
    Ui_.reserve(static_cast<size_t>(nnz_) * 4 + n);
    Ux_.reserve(static_cast<size_t>(nnz_) * 4 + n);

    pinv_.assign(n, -1);
    std::vector<double> x(n, 0.0);
    std::vector<int32_t> xi(n);        // reach stack
    std::vector<int32_t> pstack(n);    // dfs work stack
    std::vector<char> visited(n, 0);

    for (int32_t k = 0; k < n; ++k) {
        int32_t col = q_[k];

        // Mark the start of L(:,k) and U(:,k) BEFORE the triangular solve, so
        // that the DFS over already-computed columns sees correct column
        // boundaries (Lp[J+1] for the most-recent column J=k-1).
        Lp_[k] = static_cast<int32_t>(Li_.size());
        Up_[k] = static_cast<int32_t>(Ui_.size());

        // --- x = L \ A(:,col) (sparse triangular solve) ---
        // Compute reach of A(:,col) in the graph of L.
        int32_t top = n;
        for (int32_t i = 0; i < n; ++i) visited[i] = 0;  // reset marks
        for (int32_t p = a_colptr_[col]; p < a_colptr_[col + 1]; ++p) {
            int32_t i = a_rowidx_[p];
            if (!visited[i])
                top = lu_dfs(i, Lp_.data(), Li_.data(), pinv_.data(), top,
                             xi.data(), pstack.data(), visited);
        }
        // Scatter A(:,col) into x over the reach set.
        for (int32_t p = top; p < n; ++p) x[xi[p]] = 0.0;
        for (int32_t p = a_colptr_[col]; p < a_colptr_[col + 1]; ++p)
            x[a_rowidx_[p]] = values[p];
        // Apply diag_gmin to the diagonal entry of this column, matching
        // NeoSolver::add_diag_gmin (which adds gmin to every diagonal element).
        if (diag_gmin != 0.0) x[col] += diag_gmin;

        // Eliminate using already-computed pivot columns, in topological order.
        for (int32_t px = top; px < n; ++px) {
            int32_t j = xi[px];        // x(j) nonzero (original row index)
            int32_t J = pinv_[j];      // L-column for this row, or -1
            if (J < 0) continue;       // row j not yet pivotal
            double xj = x[j];
            // L(:,J): first entry is the unit diagonal (skip it).
            for (int32_t p = Lp_[J] + 1; p < Lp_[J + 1]; ++p)
                x[Li_[p]] -= Lx_[p] * xj;
        }

        // --- Find pivot among non-pivotal rows (threshold partial pivoting) ---
        int32_t ipiv = -1;
        double amax = -1.0;
        for (int32_t p = top; p < n; ++p) {
            int32_t i = xi[p];
            if (pinv_[i] < 0) {
                double t = std::fabs(x[i]);
                if (t > amax) { amax = t; ipiv = i; }
            } else {
                // x(i) is the entry U(pinv[i], k).
                Ui_.push_back(pinv_[i]);
                Ux_.push_back(x[i]);
            }
        }
        if (ipiv == -1 || amax <= 0.0) {
            // Structural / numeric singularity.
            return true;
        }
        // Prefer the diagonal (original row == col) when within tol of the max.
        if (pinv_[col] < 0 && std::fabs(x[col]) >= amax * pivot_tol_)
            ipiv = col;

        double pivot = x[ipiv];
        if (pivot == 0.0) return true;

        // U(k,k) is the last entry of U(:,k).
        Ui_.push_back(k);
        Ux_.push_back(pivot);
        pinv_[ipiv] = k;

        // L(k,k) = 1 is the first entry of L(:,k).
        Li_.push_back(ipiv);
        Lx_.push_back(1.0);
        for (int32_t p = top; p < n; ++p) {
            int32_t i = xi[p];
            if (pinv_[i] < 0) {        // x(i) belongs to L(:,k)
                Li_.push_back(i);
                Lx_.push_back(x[i] / pivot);
            }
            x[i] = 0.0;                // restore x for next k
        }
    }
    Lp_[n] = static_cast<int32_t>(Li_.size());
    Up_[n] = static_cast<int32_t>(Ui_.size());

    // Re-map L's row indices from original-row space to pivot-step space, so L
    // and U share the same (pivot-step) index space for substitution.
    for (size_t p = 0; p < Li_.size(); ++p) Li_[p] = pinv_[Li_[p]];

    factored_ = true;
    return false;
}

bool AmdLuSolver::numeric(const SparsityPattern& /*pattern*/,
                          const NumericMatrix& mat, double diag_gmin) {
    if (!symbolized_)
        throw std::logic_error("AmdLuSolver::numeric: symbolic() not called");
    return factor_(mat, diag_gmin);
}

bool AmdLuSolver::refactorize(const NumericMatrix& mat, double diag_gmin) {
    if (!symbolized_)
        throw std::logic_error("AmdLuSolver::refactorize: symbolic() not called");
    // Stage 2: no symbolic reuse — just refactor with pivoting.
    return factor_(mat, diag_gmin);
}

void AmdLuSolver::solve(std::vector<double>& rhs) {
    if (!factored_)
        throw std::logic_error("AmdLuSolver::solve: numeric() not called");
    if (static_cast<int32_t>(rhs.size()) != n_)
        throw std::invalid_argument("AmdLuSolver::solve: rhs size mismatch");

    const int32_t n = n_;
    x_.assign(n, 0.0);

    // The factorization solved P A Q = L U, where:
    //   - Q is the AMD column permutation: column q_[k] of A is the k-th column.
    //   - P is the row pivot permutation: pinv_[orig_row] = pivot step.
    // To solve A z = b:  (A Q)(Q^T z) = b -> P(A Q) y = P b -> L U y = P b,
    // where y = Q^T z, i.e. y[k] is the value for original column q_[k].
    //
    // Row side: b is in original-row order; permute to pivot-step order via
    //   bp[pinv_[i]] = b[i].
    for (int32_t i = 0; i < n; ++i)
        x_[pinv_[i]] = rhs[i];

    // Forward solve L y = bp (L unit lower, indices in pivot-step space).
    for (int32_t j = 0; j < n; ++j) {
        // L(j,j) == 1 (first entry); skip division.
        double xj = x_[j];
        for (int32_t p = Lp_[j] + 1; p < Lp_[j + 1]; ++p)
            x_[Li_[p]] -= Lx_[p] * xj;
    }

    // Backward solve U y = (forward result). U(j,j) is the last entry of col j.
    for (int32_t j = n - 1; j >= 0; --j) {
        double diag = Ux_[Up_[j + 1] - 1];
        x_[j] /= diag;
        double xj = x_[j];
        for (int32_t p = Up_[j]; p < Up_[j + 1] - 1; ++p)
            x_[Ui_[p]] -= Ux_[p] * xj;
    }

    // x_ now holds y in pivot-step / column-elimination order: y[k] is the
    // solution for original column q_[k]. Scatter back to original order.
    for (int32_t k = 0; k < n; ++k)
        rhs[q_[k]] = x_[k];
}

// ---- Complex ops delegate to the Markowitz solver ----
void AmdLuSolver::numeric_complex(const SparsityPattern& pattern,
                                  const std::vector<double>& ax) {
    complex_solver_->numeric_complex(pattern, ax);
}

bool AmdLuSolver::refactorize_complex(const std::vector<double>& ax) {
    return complex_solver_->refactorize_complex(ax);
}

void AmdLuSolver::solve_complex(std::vector<double>& rhs) {
    complex_solver_->solve_complex(rhs);
}

}  // namespace neospice
