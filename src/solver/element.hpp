#pragma once
#include "solver/config.hpp"
#include <cmath>
#include <cstdint>
#include <vector>

namespace neospice::solver {

struct MatrixElement {
    double Real = 0.0;
    double Imag = 0.0;
    int32_t Row = 0;
    int32_t Col = 0;
    MatrixElement* NextInRow = nullptr;
    MatrixElement* NextInCol = nullptr;
};

inline double element_mag(const MatrixElement* e) {
    return std::abs(e->Real) + std::abs(e->Imag);
}

namespace complex_ops {

inline double mag(double r, double i) {
    return std::abs(r) + std::abs(i);
}

inline void mult_assign(double& to_r, double& to_i,
                         double fr, double fi) {
    double tmp = to_r;
    to_r = tmp * fr - to_i * fi;
    to_i = tmp * fi + to_i * fr;
}

inline void mult_subt_assign(double& to_r, double& to_i,
                              double a_r, double a_i,
                              double b_r, double b_i) {
    to_r -= a_r * b_r - a_i * b_i;
    to_i -= a_r * b_i + a_i * b_r;
}

inline void reciprocal(double& to_r, double& to_i,
                        double den_r, double den_i) {
    if ((den_r >= den_i && den_r > -den_i) ||
        (den_r < den_i && den_r <= -den_i)) {
        double r = den_i / den_r;
        to_r = 1.0 / (den_r + r * den_i);
        to_i = -r * to_r;
    } else {
        double r = den_r / den_i;
        to_i = -1.0 / (den_i + r * den_r);
        to_r = -r * to_i;
    }
}

inline void div_assign(double& num_r, double& num_i,
                        double den_r, double den_i) {
    if ((den_r >= den_i && den_r > -den_i) ||
        (den_r < den_i && den_r <= -den_i)) {
        double r = den_i / den_r;
        double s = den_r + r * den_i;
        double t = (num_r + r * num_i) / s;
        num_i = (num_i - r * num_r) / s;
        num_r = t;
    } else {
        double r = den_r / den_i;
        double s = den_i + r * den_r;
        double t = (r * num_r + num_i) / s;
        num_i = (r * num_i - num_r) / s;
        num_r = t;
    }
}

} // namespace complex_ops

class ElementArena {
public:
    ElementArena() = default;

    void init(int32_t initial_elements, int32_t initial_fillins) {
        alloc_element_block(initial_elements);
        alloc_fillin_block(initial_fillins);
    }

    MatrixElement* get_element() {
        if (elem_remaining_ == 0)
            alloc_element_block(config::ELEMENTS_PER_ALLOCATION);
        --elem_remaining_;
        return &element_blocks_[current_elem_block_][elem_next_++];
    }

    MatrixElement* get_fillin() {
        if (fill_remaining_ == 0)
            alloc_fillin_block(config::ELEMENTS_PER_ALLOCATION);
        --fill_remaining_;
        return &fillin_blocks_[current_fill_block_][fill_next_++];
    }

private:
    void alloc_element_block(int32_t count) {
        element_blocks_.emplace_back(count);
        current_elem_block_ = static_cast<int32_t>(element_blocks_.size()) - 1;
        elem_remaining_ = count;
        elem_next_ = 0;
    }

    void alloc_fillin_block(int32_t count) {
        fillin_blocks_.emplace_back(count);
        current_fill_block_ = static_cast<int32_t>(fillin_blocks_.size()) - 1;
        fill_remaining_ = count;
        fill_next_ = 0;
    }

    std::vector<std::vector<MatrixElement>> element_blocks_;
    int32_t current_elem_block_ = -1;
    int32_t elem_remaining_ = 0;
    int32_t elem_next_ = 0;

    std::vector<std::vector<MatrixElement>> fillin_blocks_;
    int32_t current_fill_block_ = -1;
    int32_t fill_remaining_ = 0;
    int32_t fill_next_ = 0;
};

} // namespace neospice::solver
