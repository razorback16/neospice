#include "solver/matrix.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <tuple>
#include <vector>

namespace neospice::solver {

void SparseMatrix::print(bool reordered, bool data, bool header) const {
    using Entry = std::tuple<int32_t, int32_t, double, double>;
    std::vector<Entry> entries;
    entries.reserve(static_cast<std::size_t>(elements_));

    for (int32_t col = 1; col <= size_; ++col) {
        for (auto* elem = first_in_col_[col]; elem != nullptr;
             elem = elem->NextInCol) {
            const int32_t row_out = reordered ? elem->Row
                                               : int_to_ext_row_[elem->Row];
            const int32_t col_out = reordered ? col
                                               : int_to_ext_col_[col];
            entries.emplace_back(row_out, col_out, elem->Real, elem->Imag);
        }
    }
    std::sort(entries.begin(), entries.end());

    if (header) {
        std::cerr << "matrix " << size_ << " "
                  << (complex_ ? "complex" : "real") << "\n";
    }
    std::cerr << std::setprecision(17);
    for (const auto& [row, col, real, imag] : entries) {
        std::cerr << row << " " << col;
        if (data) {
            std::cerr << " " << real;
            if (complex_) std::cerr << " " << imag;
        }
        std::cerr << "\n";
    }
}

} // namespace neospice::solver
