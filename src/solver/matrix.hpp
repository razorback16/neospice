#pragma once
#include "solver/config.hpp"
#include "solver/error.hpp"
#include "solver/element.hpp"
#include <cstdint>
#include <utility>
#include <vector>

namespace neospice::solver {

class SparseMatrix {
public:
    explicit SparseMatrix(int32_t size, bool complex = true);
    ~SparseMatrix() = default;

    SparseMatrix(const SparseMatrix&) = delete;
    SparseMatrix& operator=(const SparseMatrix&) = delete;
    SparseMatrix(SparseMatrix&&) = default;
    SparseMatrix& operator=(SparseMatrix&&) = default;

    // --- Build phase ---
    double* get_element(int32_t row, int32_t col);
    double* find_element(int32_t row, int32_t col);
    void clear();
    void clear_for_load();

    // --- Factorization ---
    [[nodiscard]] SparseError order_and_factor(double* rhs,
                                                double rel_threshold,
                                                double abs_threshold,
                                                bool diag_pivoting);
    [[nodiscard]] SparseError factor();
    void partition(int mode);
    void mna_preorder();
    void add_diag_gmin(double gmin);

    // --- Solve ---
    void solve(double* rhs, double* solution,
               double* irhs = nullptr, double* isolution = nullptr);
    void solve_transposed(double* rhs, double* solution,
                          double* irhs = nullptr, double* isolution = nullptr);

    // --- Mode ---
    void set_real() { complex_ = false; }
    void set_complex() { complex_ = true; }
    bool is_complex() const { return complex_; }

    // --- Queries ---
    int32_t size() const { return size_; }
    int32_t ext_size() const { return ext_size_; }
    SparseError error() const { return error_; }
    int32_t element_count() const { return elements_; }
    int32_t fillin_count() const { return fillins_; }
    int32_t original_count() const { return originals_; }
    std::pair<int32_t, int32_t> where_singular() const;
    MatrixElement& trash_can() { return trash_can_; }

    // --- Debug ---
    void print(bool reordered, bool data, bool header) const;

private:
    // --- Dimensions ---
    int32_t size_ = 0;
    int32_t allocated_size_ = 0;
    int32_t ext_size_ = 0;
    int32_t allocated_ext_size_ = 0;
    int32_t current_size_ = 0;

    // --- Flags ---
    bool complex_ = true;
    bool prev_complex_ = true;
    bool factored_ = false;
    bool needs_ordering_ = true;
    bool reordered_ = false;
    bool partitioned_ = false;
    bool rows_linked_ = false;
    bool internal_vectors_allocated_ = false;
    bool interchanges_odd_ = false;

    // --- Error ---
    SparseError error_ = SparseError::OK;
    int32_t singular_row_ = 0;
    int32_t singular_col_ = 0;

    // --- Orthogonal linked list structure (1-indexed, [0] unused) ---
    std::vector<MatrixElement*> first_in_col_;
    std::vector<MatrixElement*> first_in_row_;
    std::vector<MatrixElement*> diag_;

    // --- Translation maps (1-indexed) ---
    std::vector<int32_t> int_to_ext_col_;
    std::vector<int32_t> int_to_ext_row_;
    std::vector<int32_t> ext_to_int_col_;
    std::vector<int32_t> ext_to_int_row_;

    // --- Markowitz data ---
    std::vector<int32_t> markowitz_row_;
    std::vector<int32_t> markowitz_col_;
    std::vector<long> markowitz_prod_;
    int32_t singletons_ = 0;
    int32_t max_row_count_lower_tri_ = 0;

    // --- Partition data ---
    std::vector<int32_t> do_real_direct_;
    std::vector<int32_t> do_cmplx_direct_;

    // --- Thresholds ---
    double rel_threshold_ = config::DEFAULT_THRESHOLD;
    double abs_threshold_ = 0.0;

    // --- Element counts ---
    int32_t elements_ = 0;
    int32_t originals_ = 0;
    int32_t fillins_ = 0;

    // --- Workspace ---
    std::vector<double> intermediate_;
    std::vector<double*> intermediate_ptrs_;

    // --- Pivot info ---
    int32_t pivots_original_row_ = 0;
    int32_t pivots_original_col_ = 0;
    char pivot_selection_method_ = ' ';

    // --- Trash can (ground node sink) ---
    MatrixElement trash_can_{};

    // --- Memory arena ---
    ElementArena arena_;

    // --- Build internals ---
    MatrixElement* find_element_in_col(MatrixElement** last_addr,
                                        int32_t row, int32_t col, bool create);
    MatrixElement* create_element(int32_t row, int32_t col,
                                   MatrixElement** last_addr, bool fillin);
    void link_rows();
    void create_internal_vectors();
    void enlarge_matrix(int32_t new_size);
    void expand_translation_arrays(int32_t new_size);
    void translate(int32_t& row, int32_t& col);

    // --- Factor internals ---
    SparseError factor_complex();
    void count_markowitz(double* rhs, int32_t step);
    void markowitz_products(int32_t step);
    MatrixElement* search_for_pivot(int32_t step, bool diag_pivoting);
    MatrixElement* search_for_singleton(int32_t step);
    MatrixElement* quickly_search_diagonal(int32_t step);
    MatrixElement* search_diagonal(int32_t step);
    MatrixElement* search_entire_matrix(int32_t step);
    double find_largest_in_col(MatrixElement* elem);
    double find_biggest_in_col_exclude(MatrixElement* elem, int32_t step);
    void exchange_rows_and_cols(MatrixElement* pivot, int32_t step);
    void row_exchange(int32_t row1, int32_t row2);
    void col_exchange(int32_t col1, int32_t col2);
    void exchange_col_elements(int32_t row1, MatrixElement* elem1,
                               int32_t row2, MatrixElement* elem2, int32_t col);
    void exchange_row_elements(int32_t col1, MatrixElement* elem1,
                               int32_t col2, MatrixElement* elem2, int32_t row);
    void real_row_col_elimination(MatrixElement* pivot);
    void complex_row_col_elimination(MatrixElement* pivot);
    void update_markowitz_numbers(MatrixElement* pivot);
    MatrixElement* create_fillin(int32_t row, int32_t col);
    SparseError matrix_is_singular(int32_t step);
    SparseError zero_pivot(int32_t step);

    // --- Solve internals ---
    void solve_complex(double* rhs, double* solution,
                       double* irhs, double* isolution);
    void solve_complex_transposed(double* rhs, double* solution,
                                   double* irhs, double* isolution);

    // --- Utils internals ---
    int32_t count_twins(int32_t col, MatrixElement** twin1, MatrixElement** twin2);
    void swap_cols(MatrixElement* elem1, MatrixElement* elem2);
};

} // namespace neospice::solver
