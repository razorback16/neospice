#include "solver/matrix.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace neospice::solver {

// ============================================================================
// Error helpers
// ============================================================================

SparseError SparseMatrix::matrix_is_singular(int32_t step) {
    singular_row_ = int_to_ext_row_[step];
    singular_col_ = int_to_ext_col_[step];
    return (error_ = SparseError::Singular);
}

SparseError SparseMatrix::zero_pivot(int32_t step) {
    singular_row_ = int_to_ext_row_[step];
    singular_col_ = int_to_ext_col_[step];
    return (error_ = SparseError::ZeroDiag);
}

// ============================================================================
// Column magnitude helpers
// ============================================================================

double SparseMatrix::find_largest_in_col(MatrixElement* elem) {
    double largest = 0.0;
    while (elem) {
        double mag = element_mag(elem);
        if (mag > largest) largest = mag;
        elem = elem->NextInCol;
    }
    return largest;
}

double SparseMatrix::find_biggest_in_col_exclude(MatrixElement* p_element,
                                                  int32_t step) {
    int32_t row = p_element->Row;
    int32_t col = p_element->Col;
    auto* elem = first_in_col_[col];

    while (elem && elem->Row < step)
        elem = elem->NextInCol;

    double largest = (elem->Row != row) ? element_mag(elem) : 0.0;

    while ((elem = elem->NextInCol) != nullptr) {
        double mag = element_mag(elem);
        if (mag > largest && elem->Row != row)
            largest = mag;
    }
    return largest;
}

// ============================================================================
// Markowitz counting
// ============================================================================

void SparseMatrix::count_markowitz(double* rhs, int32_t step) {
    int32_t size = size_;

    for (int32_t i = step; i <= size; i++) {
        int32_t count = -1;
        auto* elem = first_in_row_[i];
        while (elem && elem->Col < step)
            elem = elem->NextInRow;
        while (elem) {
            count++;
            elem = elem->NextInRow;
        }

        int32_t ext_row = int_to_ext_row_[i];
        if (rhs && rhs[ext_row] != 0.0)
            count++;
        markowitz_row_[i] = count;
    }

    for (int32_t i = step; i <= size; i++) {
        int32_t count = -1;
        auto* elem = first_in_col_[i];
        while (elem && elem->Row < step)
            elem = elem->NextInCol;
        while (elem) {
            count++;
            elem = elem->NextInCol;
        }
        markowitz_col_[i] = count;
    }
}

void SparseMatrix::markowitz_products(int32_t step) {
    int32_t size = size_;
    singletons_ = 0;

    for (int32_t i = step; i <= size; i++) {
        int32_t mr = markowitz_row_[i];
        int32_t mc = markowitz_col_[i];

        if ((mr > config::LARGEST_SHORT_INTEGER && mc != 0) ||
            (mc > config::LARGEST_SHORT_INTEGER && mr != 0)) {
            double fp = static_cast<double>(mr) * static_cast<double>(mc);
            if (fp >= config::LARGEST_LONG_INTEGER)
                markowitz_prod_[i] = config::LARGEST_LONG_INTEGER;
            else
                markowitz_prod_[i] = static_cast<long>(fp);
        } else {
            long product = static_cast<long>(mr) * mc;
            markowitz_prod_[i] = product;
            if (product == 0)
                singletons_++;
        }
    }
}

// ============================================================================
// Pivot search
// ============================================================================

MatrixElement* SparseMatrix::search_for_singleton(int32_t step) {
    int32_t size = size_;

    markowitz_prod_[size + 1] = markowitz_prod_[step];
    markowitz_prod_[step - 1] = 0;

    int32_t singletons = singletons_--;
    int32_t scan = size + 1;

    while (singletons-- > 0) {
        while (markowitz_prod_[scan] != 0)
            --scan;
        int32_t i = scan;
        --scan;

        if (i < step) break;
        if (i > size) i = step;

        MatrixElement* chosen;
        double pivot_mag;

        if ((chosen = diag_[i]) != nullptr) {
            pivot_mag = element_mag(chosen);
            if (pivot_mag > abs_threshold_ &&
                pivot_mag > rel_threshold_ *
                    find_biggest_in_col_exclude(chosen, step))
                return chosen;
        } else {
            if (markowitz_col_[i] == 0) {
                chosen = first_in_col_[i];
                while (chosen && chosen->Row < step)
                    chosen = chosen->NextInCol;
                if (chosen != nullptr)
                    break;
                pivot_mag = element_mag(chosen);
                if (pivot_mag > abs_threshold_ &&
                    pivot_mag > rel_threshold_ *
                        find_biggest_in_col_exclude(chosen, step))
                    return chosen;
                else {
                    if (markowitz_row_[i] == 0) {
                        chosen = first_in_row_[i];
                        while (chosen && chosen->Col < step)
                            chosen = chosen->NextInRow;
                        if (chosen != nullptr)
                            break;
                        pivot_mag = element_mag(chosen);
                        if (pivot_mag > abs_threshold_ &&
                            pivot_mag > rel_threshold_ *
                                find_biggest_in_col_exclude(chosen, step))
                            return chosen;
                    }
                }
            } else {
                chosen = first_in_row_[i];
                while (chosen && chosen->Col < step)
                    chosen = chosen->NextInRow;
                if (chosen != nullptr)
                    break;
                pivot_mag = element_mag(chosen);
                if (pivot_mag > abs_threshold_ &&
                    pivot_mag > rel_threshold_ *
                        find_biggest_in_col_exclude(chosen, step))
                    return chosen;
            }
        }
    }

    singletons_++;
    return nullptr;
}

MatrixElement* SparseMatrix::quickly_search_diagonal(int32_t step) {
    int32_t size = size_;
    MatrixElement* chosen_pivot = nullptr;
    long min_markowitz_product = config::LARGEST_LONG_INTEGER;

    markowitz_prod_[size + 1] = markowitz_prod_[step];
    markowitz_prod_[step - 1] = -1;

    int32_t scan = size + 2;

    for (;;) {
        --scan;
        while (markowitz_prod_[scan] >= min_markowitz_product)
            --scan;

        int32_t i = scan;
        if (i < step) break;
        if (i > size) i = step;

        auto* diag = diag_[i];
        if (!diag) continue;

        double magnitude = element_mag(diag);
        if (magnitude <= abs_threshold_) continue;

        if (markowitz_prod_[scan] == 1) {
            auto* other_in_row = diag->NextInRow;
            auto* other_in_col = diag->NextInCol;
            if (!other_in_row && !other_in_col) {
                other_in_row = first_in_row_[i];
                while (other_in_row) {
                    if (other_in_row->Col >= step && other_in_row->Col != i)
                        break;
                    other_in_row = other_in_row->NextInRow;
                }
                other_in_col = first_in_col_[i];
                while (other_in_col) {
                    if (other_in_col->Row >= step && other_in_col->Row != i)
                        break;
                    other_in_col = other_in_col->NextInCol;
                }
            }

            if (other_in_row && other_in_col) {
                if (other_in_row->Col == other_in_col->Row) {
                    double largest_off_diag = std::max(
                        element_mag(other_in_row), element_mag(other_in_col));
                    if (magnitude >= largest_off_diag)
                        return diag;
                }
            }
        }

        min_markowitz_product = markowitz_prod_[scan];
        chosen_pivot = diag;
    }

    if (chosen_pivot) {
        double largest_in_col = find_biggest_in_col_exclude(chosen_pivot, step);
        if (element_mag(chosen_pivot) <= rel_threshold_ * largest_in_col)
            chosen_pivot = nullptr;
    }
    return chosen_pivot;
}

MatrixElement* SparseMatrix::search_diagonal(int32_t step) {
    int32_t size = size_;
    MatrixElement* chosen_pivot = nullptr;
    long min_markowitz_product = config::LARGEST_LONG_INTEGER;
    int32_t number_of_ties = 0;
    double ratio_of_accepted = 0.0;

    long* mp = markowitz_prod_.data();
    mp[size + 1] = mp[step];

    for (int32_t j = size + 1; j > step; j--) {
        if (mp[j] > min_markowitz_product)
            continue;

        int32_t i = (j > size) ? step : j;

        auto* diag = diag_[i];
        if (!diag) continue;

        double magnitude = element_mag(diag);
        if (magnitude <= abs_threshold_) continue;

        double largest_in_col = find_biggest_in_col_exclude(diag, step);
        if (magnitude <= rel_threshold_ * largest_in_col)
            continue;

        if (mp[j] < min_markowitz_product) {
            chosen_pivot = diag;
            min_markowitz_product = mp[j];
            ratio_of_accepted = largest_in_col / magnitude;
            number_of_ties = 0;
        } else {
            number_of_ties++;
            double ratio = largest_in_col / magnitude;
            if (ratio < ratio_of_accepted) {
                chosen_pivot = diag;
                ratio_of_accepted = ratio;
            }
            if (number_of_ties >= min_markowitz_product * config::TIES_MULTIPLIER)
                return chosen_pivot;
        }
    }
    return chosen_pivot;
}

MatrixElement* SparseMatrix::search_entire_matrix(int32_t step) {
    int32_t size = size_;
    MatrixElement* chosen_pivot = nullptr;
    MatrixElement* largest_element = nullptr;
    double largest_element_mag = 0.0;
    long min_markowitz_product = config::LARGEST_LONG_INTEGER;
    int32_t number_of_ties = 0;
    double ratio_of_accepted = 0.0;

    for (int32_t i = step; i <= size; i++) {
        auto* elem = first_in_col_[i];

        while (elem && elem->Row < step)
            elem = elem->NextInCol;

        double largest_in_col = find_largest_in_col(elem);
        if (largest_in_col == 0.0)
            continue;

        while (elem) {
            double magnitude = element_mag(elem);
            if (magnitude > largest_element_mag) {
                largest_element_mag = magnitude;
                largest_element = elem;
            }

            long product = static_cast<long>(markowitz_row_[elem->Row]) *
                           markowitz_col_[elem->Col];

            if (product <= min_markowitz_product &&
                magnitude > rel_threshold_ * largest_in_col &&
                magnitude > abs_threshold_) {
                if (product < min_markowitz_product) {
                    chosen_pivot = elem;
                    min_markowitz_product = product;
                    ratio_of_accepted = largest_in_col / magnitude;
                    number_of_ties = 0;
                } else {
                    number_of_ties++;
                    double ratio = largest_in_col / magnitude;
                    if (ratio < ratio_of_accepted) {
                        chosen_pivot = elem;
                        ratio_of_accepted = ratio;
                    }
                    if (number_of_ties >= min_markowitz_product * config::TIES_MULTIPLIER)
                        return chosen_pivot;
                }
            }
            elem = elem->NextInCol;
        }
    }

    if (chosen_pivot) return chosen_pivot;

    if (largest_element_mag == 0.0) {
        error_ = SparseError::Singular;
        return nullptr;
    }

    error_ = SparseError::SmallPivot;
    return largest_element;
}

MatrixElement* SparseMatrix::search_for_pivot(int32_t step, bool diag_pivoting) {
    MatrixElement* chosen;

    if (singletons_) {
        chosen = search_for_singleton(step);
        if (chosen) {
            pivot_selection_method_ = 's';
            return chosen;
        }
    }

    if constexpr (config::DIAGONAL_PIVOTING) {
        if (diag_pivoting) {
            chosen = quickly_search_diagonal(step);
            if (chosen) {
                pivot_selection_method_ = 'q';
                return chosen;
            }

            chosen = search_diagonal(step);
            if (chosen) {
                pivot_selection_method_ = 'd';
                return chosen;
            }
        }
    }

    chosen = search_entire_matrix(step);
    pivot_selection_method_ = 'e';
    return chosen;
}

// ============================================================================
// Row/column element exchange (linked list surgery)
// ============================================================================

void SparseMatrix::exchange_col_elements(
    int32_t row1, MatrixElement* elem1,
    int32_t row2, MatrixElement* elem2, int32_t col) {

    MatrixElement** above_row1 = &first_in_col_[col];
    auto* scan = *above_row1;
    while (scan->Row < row1) {
        above_row1 = &scan->NextInCol;
        scan = *above_row1;
    }

    if (elem1) {
        auto* below_row1 = elem1->NextInCol;
        if (!elem2) {
            if (below_row1 && below_row1->Row < row2) {
                *above_row1 = below_row1;
                auto* p = below_row1;
                MatrixElement** above_row2;
                do {
                    above_row2 = &p->NextInCol;
                    p = *above_row2;
                } while (p && p->Row < row2);
                *above_row2 = elem1;
                elem1->NextInCol = p;
                *above_row1 = below_row1;
            }
            elem1->Row = row2;
        } else {
            if (below_row1->Row == row2) {
                elem1->NextInCol = elem2->NextInCol;
                elem2->NextInCol = elem1;
                *above_row1 = elem2;
            } else {
                auto* p = below_row1;
                MatrixElement** above_row2;
                do {
                    above_row2 = &p->NextInCol;
                    p = *above_row2;
                } while (p->Row < row2);
                auto* below_row2 = elem2->NextInCol;
                *above_row1 = elem2;
                elem2->NextInCol = below_row1;
                *above_row2 = elem1;
                elem1->NextInCol = below_row2;
            }
            elem1->Row = row2;
            elem2->Row = row1;
        }
    } else {
        auto* below_row1_pos = scan;
        if (below_row1_pos->Row != row2) {
            auto* p = scan;
            MatrixElement** above_row2;
            do {
                above_row2 = &p->NextInCol;
                p = *above_row2;
            } while (p->Row < row2);
            *above_row2 = elem2->NextInCol;
            *above_row1 = elem2;
            elem2->NextInCol = below_row1_pos;
        }
        elem2->Row = row1;
    }
}

void SparseMatrix::exchange_row_elements(
    int32_t col1, MatrixElement* elem1,
    int32_t col2, MatrixElement* elem2, int32_t row) {

    MatrixElement** left_of_col1 = &first_in_row_[row];
    auto* scan = *left_of_col1;
    while (scan->Col < col1) {
        left_of_col1 = &scan->NextInRow;
        scan = *left_of_col1;
    }

    if (elem1) {
        auto* right_of_col1 = elem1->NextInRow;
        if (!elem2) {
            if (right_of_col1 && right_of_col1->Col < col2) {
                *left_of_col1 = right_of_col1;
                auto* p = right_of_col1;
                MatrixElement** left_of_col2;
                do {
                    left_of_col2 = &p->NextInRow;
                    p = *left_of_col2;
                } while (p && p->Col < col2);
                *left_of_col2 = elem1;
                elem1->NextInRow = p;
                *left_of_col1 = right_of_col1;
            }
            elem1->Col = col2;
        } else {
            if (right_of_col1->Col == col2) {
                elem1->NextInRow = elem2->NextInRow;
                elem2->NextInRow = elem1;
                *left_of_col1 = elem2;
            } else {
                auto* p = right_of_col1;
                MatrixElement** left_of_col2;
                do {
                    left_of_col2 = &p->NextInRow;
                    p = *left_of_col2;
                } while (p->Col < col2);
                auto* right_of_col2 = elem2->NextInRow;
                *left_of_col1 = elem2;
                elem2->NextInRow = right_of_col1;
                *left_of_col2 = elem1;
                elem1->NextInRow = right_of_col2;
            }
            elem1->Col = col2;
            elem2->Col = col1;
        }
    } else {
        auto* right_of_col1_pos = scan;
        if (right_of_col1_pos->Col != col2) {
            auto* p = scan;
            MatrixElement** left_of_col2;
            do {
                left_of_col2 = &p->NextInRow;
                p = *left_of_col2;
            } while (p->Col < col2);
            *left_of_col2 = elem2->NextInRow;
            *left_of_col1 = elem2;
            elem2->NextInRow = right_of_col1_pos;
        }
        elem2->Col = col1;
    }
}

// ============================================================================
// Row/column exchange
// ============================================================================

void SparseMatrix::row_exchange(int32_t row1, int32_t row2) {
    if (row1 > row2) std::swap(row1, row2);

    auto* r1_ptr = first_in_row_[row1];
    auto* r2_ptr = first_in_row_[row2];

    while (r1_ptr || r2_ptr) {
        int32_t column;
        MatrixElement* e1;
        MatrixElement* e2;

        if (!r1_ptr) {
            column = r2_ptr->Col;
            e1 = nullptr;
            e2 = r2_ptr;
            r2_ptr = r2_ptr->NextInRow;
        } else if (!r2_ptr) {
            column = r1_ptr->Col;
            e1 = r1_ptr;
            e2 = nullptr;
            r1_ptr = r1_ptr->NextInRow;
        } else if (r1_ptr->Col < r2_ptr->Col) {
            column = r1_ptr->Col;
            e1 = r1_ptr;
            e2 = nullptr;
            r1_ptr = r1_ptr->NextInRow;
        } else if (r1_ptr->Col > r2_ptr->Col) {
            column = r2_ptr->Col;
            e1 = nullptr;
            e2 = r2_ptr;
            r2_ptr = r2_ptr->NextInRow;
        } else {
            column = r1_ptr->Col;
            e1 = r1_ptr;
            e2 = r2_ptr;
            r1_ptr = r1_ptr->NextInRow;
            r2_ptr = r2_ptr->NextInRow;
        }

        exchange_col_elements(row1, e1, row2, e2, column);
    }

    if (internal_vectors_allocated_)
        std::swap(markowitz_row_[row1], markowitz_row_[row2]);
    std::swap(first_in_row_[row1], first_in_row_[row2]);
    std::swap(int_to_ext_row_[row1], int_to_ext_row_[row2]);

    if constexpr (config::TRANSLATE) {
        ext_to_int_row_[int_to_ext_row_[row1]] = row1;
        ext_to_int_row_[int_to_ext_row_[row2]] = row2;
    }
}

void SparseMatrix::col_exchange(int32_t col1, int32_t col2) {
    if (col1 > col2) std::swap(col1, col2);

    auto* c1_ptr = first_in_col_[col1];
    auto* c2_ptr = first_in_col_[col2];

    while (c1_ptr || c2_ptr) {
        int32_t row;
        MatrixElement* e1;
        MatrixElement* e2;

        if (!c1_ptr) {
            row = c2_ptr->Row;
            e1 = nullptr;
            e2 = c2_ptr;
            c2_ptr = c2_ptr->NextInCol;
        } else if (!c2_ptr) {
            row = c1_ptr->Row;
            e1 = c1_ptr;
            e2 = nullptr;
            c1_ptr = c1_ptr->NextInCol;
        } else if (c1_ptr->Row < c2_ptr->Row) {
            row = c1_ptr->Row;
            e1 = c1_ptr;
            e2 = nullptr;
            c1_ptr = c1_ptr->NextInCol;
        } else if (c1_ptr->Row > c2_ptr->Row) {
            row = c2_ptr->Row;
            e1 = nullptr;
            e2 = c2_ptr;
            c2_ptr = c2_ptr->NextInCol;
        } else {
            row = c1_ptr->Row;
            e1 = c1_ptr;
            e2 = c2_ptr;
            c1_ptr = c1_ptr->NextInCol;
            c2_ptr = c2_ptr->NextInCol;
        }

        exchange_row_elements(col1, e1, col2, e2, row);
    }

    if (internal_vectors_allocated_)
        std::swap(markowitz_col_[col1], markowitz_col_[col2]);
    std::swap(first_in_col_[col1], first_in_col_[col2]);
    std::swap(int_to_ext_col_[col1], int_to_ext_col_[col2]);

    if constexpr (config::TRANSLATE) {
        ext_to_int_col_[int_to_ext_col_[col1]] = col1;
        ext_to_int_col_[int_to_ext_col_[col2]] = col2;
    }
}

void SparseMatrix::exchange_rows_and_cols(MatrixElement* pivot, int32_t step) {
    int32_t row = pivot->Row;
    int32_t col = pivot->Col;
    pivots_original_row_ = row;
    pivots_original_col_ = col;

    if (row == step && col == step) return;

    if (row == col) {
        row_exchange(step, row);
        col_exchange(step, col);
        std::swap(markowitz_prod_[step], markowitz_prod_[row]);
        std::swap(diag_[row], diag_[step]);
    } else {
        long old_mp_step = markowitz_prod_[step];
        long old_mp_row = markowitz_prod_[row];
        long old_mp_col = markowitz_prod_[col];

        if (row != step) {
            row_exchange(step, row);
            interchanges_odd_ = !interchanges_odd_;
            markowitz_prod_[row] = static_cast<long>(markowitz_row_[row]) *
                                   markowitz_col_[row];
            if ((markowitz_prod_[row] == 0) != (old_mp_row == 0)) {
                if (old_mp_row == 0)
                    singletons_--;
                else
                    singletons_++;
            }
        }

        if (col != step) {
            col_exchange(step, col);
            interchanges_odd_ = !interchanges_odd_;
            markowitz_prod_[col] = static_cast<long>(markowitz_col_[col]) *
                                   markowitz_row_[col];
            if ((markowitz_prod_[col] == 0) != (old_mp_col == 0)) {
                if (old_mp_col == 0)
                    singletons_--;
                else
                    singletons_++;
            }
            diag_[col] = find_element_in_col(&first_in_col_[col], col, col, false);
        }
        if (row != step) {
            diag_[row] = find_element_in_col(&first_in_col_[row], row, row, false);
        }
        diag_[step] = find_element_in_col(&first_in_col_[step], step, step, false);

        markowitz_prod_[step] = static_cast<long>(markowitz_col_[step]) *
                                markowitz_row_[step];
        if ((markowitz_prod_[step] == 0) != (old_mp_step == 0)) {
            if (old_mp_step == 0)
                singletons_--;
            else
                singletons_++;
        }
    }
}

// ============================================================================
// Fill-in creation
// ============================================================================

MatrixElement* SparseMatrix::create_fillin(int32_t row, int32_t col) {
    MatrixElement** above = &first_in_col_[col];
    auto* elem = *above;
    while (elem) {
        if (elem->Row < row) {
            above = &elem->NextInCol;
            elem = *above;
        } else
            break;
    }

    elem = create_element(row, col, above, true);

    markowitz_prod_[row] = ++markowitz_row_[row] *
                           static_cast<long>(markowitz_col_[row]);
    if (markowitz_row_[row] == 1 && markowitz_col_[row] != 0)
        singletons_--;
    markowitz_prod_[col] = ++markowitz_col_[col] *
                           static_cast<long>(markowitz_row_[col]);
    if (markowitz_row_[col] != 0 && markowitz_col_[col] == 1)
        singletons_--;

    return elem;
}

// ============================================================================
// Elimination
// ============================================================================

void SparseMatrix::real_row_col_elimination(MatrixElement* pivot) {
    if (std::abs(pivot->Real) == 0.0) {
        matrix_is_singular(pivot->Row);
        return;
    }
    pivot->Real = 1.0 / pivot->Real;

    auto* upper = pivot->NextInRow;
    while (upper) {
        upper->Real *= pivot->Real;

        auto* sub = upper->NextInCol;
        auto* lower = pivot->NextInCol;
        while (lower) {
            int32_t row = lower->Row;

            while (sub && sub->Row < row)
                sub = sub->NextInCol;

            if (!sub || sub->Row > row) {
                sub = create_fillin(row, upper->Col);
                if (!sub) {
                    error_ = SparseError::NoMemory;
                    return;
                }
            }
            sub->Real -= upper->Real * lower->Real;
            sub = sub->NextInCol;
            lower = lower->NextInCol;
        }
        upper = upper->NextInRow;
    }
}

void SparseMatrix::complex_row_col_elimination(MatrixElement* pivot) {
    if (element_mag(pivot) == 0.0) {
        matrix_is_singular(pivot->Row);
        return;
    }
    complex_ops::reciprocal(pivot->Real, pivot->Imag, pivot->Real, pivot->Imag);

    auto* upper = pivot->NextInRow;
    while (upper) {
        complex_ops::mult_assign(upper->Real, upper->Imag,
                                 pivot->Real, pivot->Imag);

        auto* sub = upper->NextInCol;
        auto* lower = pivot->NextInCol;
        while (lower) {
            int32_t row = lower->Row;

            while (sub && sub->Row < row)
                sub = sub->NextInCol;

            if (!sub || sub->Row > row) {
                sub = create_fillin(row, upper->Col);
                if (!sub) {
                    error_ = SparseError::NoMemory;
                    return;
                }
            }
            complex_ops::mult_subt_assign(sub->Real, sub->Imag,
                                          upper->Real, upper->Imag,
                                          lower->Real, lower->Imag);
            sub = sub->NextInCol;
            lower = lower->NextInCol;
        }
        upper = upper->NextInRow;
    }
}

void SparseMatrix::update_markowitz_numbers(MatrixElement* pivot) {
    auto* marko_row = markowitz_row_.data();
    auto* marko_col = markowitz_col_.data();

    for (auto* col_ptr = pivot->NextInCol; col_ptr; col_ptr = col_ptr->NextInCol) {
        int32_t row = col_ptr->Row;
        --marko_row[row];

        if ((marko_row[row] > config::LARGEST_SHORT_INTEGER && marko_col[row] != 0) ||
            (marko_col[row] > config::LARGEST_SHORT_INTEGER && marko_row[row] != 0)) {
            double product = static_cast<double>(marko_col[row]) * marko_row[row];
            if (product >= config::LARGEST_LONG_INTEGER)
                markowitz_prod_[row] = config::LARGEST_LONG_INTEGER;
            else
                markowitz_prod_[row] = static_cast<long>(product);
        } else {
            markowitz_prod_[row] = static_cast<long>(marko_row[row]) * marko_col[row];
        }
        if (marko_row[row] == 0)
            singletons_++;
    }

    for (auto* row_ptr = pivot->NextInRow; row_ptr; row_ptr = row_ptr->NextInRow) {
        int32_t col = row_ptr->Col;
        --marko_col[col];

        if ((marko_row[col] > config::LARGEST_SHORT_INTEGER && marko_col[col] != 0) ||
            (marko_col[col] > config::LARGEST_SHORT_INTEGER && marko_row[col] != 0)) {
            double product = static_cast<double>(marko_col[col]) * marko_row[col];
            if (product >= config::LARGEST_LONG_INTEGER)
                markowitz_prod_[col] = config::LARGEST_LONG_INTEGER;
            else
                markowitz_prod_[col] = static_cast<long>(product);
        } else {
            markowitz_prod_[col] = static_cast<long>(marko_row[col]) * marko_col[col];
        }
        if (marko_col[col] == 0 && marko_row[col] != 0)
            singletons_++;
    }
}

// ============================================================================
// Partition
// ============================================================================

void SparseMatrix::partition(int mode) {
    if (partitioned_) return;
    int32_t size = size_;
    partitioned_ = true;

    if (mode == config::DEFAULT_PARTITION) mode = config::AUTO_PARTITION;

    if (mode == config::DIRECT_PARTITION) {
        for (int32_t i = 1; i <= size; i++) {
            do_real_direct_[i] = 1;
            do_cmplx_direct_[i] = 1;
        }
        return;
    }
    if (mode == config::INDIRECT_PARTITION) {
        for (int32_t i = 1; i <= size; i++) {
            do_real_direct_[i] = 0;
            do_cmplx_direct_[i] = 0;
        }
        return;
    }
    assert(mode == config::AUTO_PARTITION);

    auto* nc = markowitz_row_.data();
    auto* no_count = markowitz_col_.data();

    for (int32_t step = 1; step <= size; step++) {
        nc[step] = 0;
        no_count[step] = 0;
        markowitz_prod_[step] = 0;

        auto* elem = first_in_col_[step];
        while (elem) {
            nc[step]++;
            elem = elem->NextInCol;
        }

        auto* col = first_in_col_[step];
        while (col->Row < step) {
            auto* e = diag_[col->Row];
            markowitz_prod_[step]++;
            while ((e = e->NextInCol) != nullptr)
                no_count[step]++;
            col = col->NextInCol;
        }
    }

    for (int32_t step = 1; step <= size; step++) {
        int32_t nm = static_cast<int32_t>(markowitz_prod_[step]);
        do_real_direct_[step] =
            (nm + no_count[step] > 3 * nc[step] - 2 * nm) ? 1 : 0;
        do_cmplx_direct_[step] =
            (nm + no_count[step] > 7 * nc[step] - 4 * nm) ? 1 : 0;
    }
}

// ============================================================================
// Entry points: order_and_factor, factor, factor_complex
// ============================================================================

SparseError SparseMatrix::order_and_factor(double* rhs,
                                            double rel_threshold,
                                            double abs_threshold,
                                            bool diag_pivoting) {
    assert(!factored_);

    error_ = SparseError::OK;
    int32_t size = size_;

    if (rel_threshold <= 0.0) rel_threshold = rel_threshold_;
    if (rel_threshold > 1.0)  rel_threshold = rel_threshold_;
    rel_threshold_ = rel_threshold;
    if (abs_threshold < 0.0)  abs_threshold = abs_threshold_;
    abs_threshold_ = abs_threshold;

    bool reordering_required = false;
    int32_t step = 1;

    if (!needs_ordering_) {
        for (step = 1; step <= size; step++) {
            auto* pivot = diag_[step];
            if (!pivot) {
                reordering_required = true;
                break;
            }
            double largest_in_col = find_largest_in_col(pivot->NextInCol);
            if (largest_in_col * rel_threshold < element_mag(pivot)) {
                if (complex_)
                    complex_row_col_elimination(pivot);
                else
                    real_row_col_elimination(pivot);
            } else {
                reordering_required = true;
                break;
            }
        }
        if (!reordering_required) goto done;
    } else {
        step = 1;
        if (!rows_linked_) link_rows();
        if (!internal_vectors_allocated_) create_internal_vectors();
        if (is_fatal(error_)) return error_;
    }

    count_markowitz(rhs, step);
    markowitz_products(step);
    max_row_count_lower_tri_ = -1;

    for (; step <= size; step++) {
        auto* pivot = search_for_pivot(step, diag_pivoting);
        if (!pivot) return matrix_is_singular(step);
        exchange_rows_and_cols(pivot, step);

        if (complex_)
            complex_row_col_elimination(pivot);
        else
            real_row_col_elimination(pivot);

        if (is_fatal(error_)) return error_;
        update_markowitz_numbers(pivot);
    }

done:
    needs_ordering_ = false;
    reordered_ = true;
    factored_ = true;
    return error_;
}

SparseError SparseMatrix::factor() {
    assert(!factored_);

    if (needs_ordering_) {
        return order_and_factor(nullptr, 0.0, 0.0,
                                config::DIAG_PIVOTING_AS_DEFAULT);
    }
    if (!partitioned_) partition(config::DEFAULT_PARTITION);
    if (complex_) return factor_complex();

    int32_t size = size_;

    if (size == 0) {
        factored_ = true;
        return (error_ = SparseError::OK);
    }

    if (diag_[1]->Real == 0.0) return zero_pivot(1);
    diag_[1]->Real = 1.0 / diag_[1]->Real;

    for (int32_t step = 2; step <= size; step++) {
        if (do_real_direct_[step]) {
            double* dest = intermediate_.data();

            auto* elem = first_in_col_[step];
            while (elem) {
                dest[elem->Row] = elem->Real;
                elem = elem->NextInCol;
            }

            auto* col_elem = first_in_col_[step];
            while (col_elem->Row < step) {
                auto* pivot_elem = diag_[col_elem->Row];
                col_elem->Real = dest[col_elem->Row] * pivot_elem->Real;
                while ((pivot_elem = pivot_elem->NextInCol) != nullptr)
                    dest[pivot_elem->Row] -= col_elem->Real * pivot_elem->Real;
                col_elem = col_elem->NextInCol;
            }

            elem = diag_[step]->NextInCol;
            while (elem) {
                elem->Real = dest[elem->Row];
                elem = elem->NextInCol;
            }

            if (dest[step] == 0.0) return zero_pivot(step);
            diag_[step]->Real = 1.0 / dest[step];
        } else {
            double** pdest = intermediate_ptrs_.data();

            auto* elem = first_in_col_[step];
            while (elem) {
                pdest[elem->Row] = &elem->Real;
                elem = elem->NextInCol;
            }

            auto* col_elem = first_in_col_[step];
            while (col_elem->Row < step) {
                auto* pivot_elem = diag_[col_elem->Row];
                double mult = (*pdest[col_elem->Row] *= pivot_elem->Real);
                while ((pivot_elem = pivot_elem->NextInCol) != nullptr)
                    *pdest[pivot_elem->Row] -= mult * pivot_elem->Real;
                col_elem = col_elem->NextInCol;
            }

            if (diag_[step]->Real == 0.0) return zero_pivot(step);
            diag_[step]->Real = 1.0 / diag_[step]->Real;
        }
    }

    factored_ = true;
    return (error_ = SparseError::OK);
}

SparseError SparseMatrix::factor_complex() {
    assert(complex_);

    int32_t size = size_;

    if (size == 0) {
        factored_ = true;
        return (error_ = SparseError::OK);
    }

    auto* pivot1 = diag_[1];
    if (element_mag(pivot1) == 0.0) return zero_pivot(1);
    complex_ops::reciprocal(pivot1->Real, pivot1->Imag,
                            pivot1->Real, pivot1->Imag);

    for (int32_t step = 2; step <= size; step++) {
        if (do_cmplx_direct_[step]) {
            double* dest = intermediate_.data();

            auto* elem = first_in_col_[step];
            while (elem) {
                dest[2 * elem->Row] = elem->Real;
                dest[2 * elem->Row + 1] = elem->Imag;
                elem = elem->NextInCol;
            }

            auto* col_elem = first_in_col_[step];
            while (col_elem->Row < step) {
                auto* piv = diag_[col_elem->Row];
                int32_t ri = 2 * col_elem->Row;
                double mult_r = dest[ri] * piv->Real - dest[ri + 1] * piv->Imag;
                double mult_i = dest[ri] * piv->Imag + dest[ri + 1] * piv->Real;
                col_elem->Real = mult_r;
                col_elem->Imag = mult_i;
                while ((piv = piv->NextInCol) != nullptr) {
                    int32_t di = 2 * piv->Row;
                    dest[di]     -= mult_r * piv->Real - mult_i * piv->Imag;
                    dest[di + 1] -= mult_r * piv->Imag + mult_i * piv->Real;
                }
                col_elem = col_elem->NextInCol;
            }

            auto* ge = diag_[step]->NextInCol;
            while (ge) {
                ge->Real = dest[2 * ge->Row];
                ge->Imag = dest[2 * ge->Row + 1];
                ge = ge->NextInCol;
            }

            double piv_r = dest[2 * step], piv_i = dest[2 * step + 1];
            if (std::abs(piv_r) + std::abs(piv_i) == 0.0) return zero_pivot(step);
            complex_ops::reciprocal(diag_[step]->Real, diag_[step]->Imag,
                                    piv_r, piv_i);
        } else {
            double** pdest = intermediate_ptrs_.data();

            auto* elem = first_in_col_[step];
            while (elem) {
                pdest[elem->Row] = &elem->Real;
                elem = elem->NextInCol;
            }

            auto* col_elem = first_in_col_[step];
            while (col_elem->Row < step) {
                auto* piv = diag_[col_elem->Row];
                double* src = pdest[col_elem->Row];
                double mult_r = src[0] * piv->Real - src[1] * piv->Imag;
                double mult_i = src[0] * piv->Imag + src[1] * piv->Real;
                src[0] = mult_r;
                src[1] = mult_i;
                while ((piv = piv->NextInCol) != nullptr) {
                    double* d = pdest[piv->Row];
                    d[0] -= mult_r * piv->Real - mult_i * piv->Imag;
                    d[1] -= mult_r * piv->Imag + mult_i * piv->Real;
                }
                col_elem = col_elem->NextInCol;
            }

            auto* diag_elem = diag_[step];
            if (element_mag(diag_elem) == 0.0) return zero_pivot(step);
            complex_ops::reciprocal(diag_elem->Real, diag_elem->Imag,
                                    diag_elem->Real, diag_elem->Imag);
        }
    }

    factored_ = true;
    return (error_ = SparseError::OK);
}

} // namespace neospice::solver
