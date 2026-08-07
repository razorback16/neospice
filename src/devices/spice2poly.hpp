#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace neospice {

// Literal equivalents of the EVTERM/NXTPWR helpers used by ngspice's
// XSPICE spice2poly code model.  Keeping the same operation order matters for
// vendor macromodels whose Newton trajectory is sensitive to the last bit.
inline double spice2poly_evterm(double x, int n) {
    double product = 1.0;
    while (n > 0) {
        product *= x;
        --n;
    }
    return product;
}

inline void spice2poly_nxtpwr(std::vector<int>& pwrseq) {
    const int pdim = static_cast<int>(pwrseq.size());
    if (pdim == 1) {
        ++pwrseq[0];
        return;
    }

    int k = pdim;
    while (pwrseq[static_cast<std::size_t>(k - 1)] == 0) {
        --k;
        if (k == 0) {
            ++pwrseq[0];
            return;
        }
    }

    if (k != pdim) {
        --pwrseq[static_cast<std::size_t>(k - 1)];
        ++pwrseq[static_cast<std::size_t>(k)];
        return;
    }

    const int km1 = k - 1;
    int i = 1;
    while (i <= km1 && pwrseq[static_cast<std::size_t>(i - 1)] == 0)
        ++i;
    if (i > km1) {
        pwrseq[0] = pwrseq[static_cast<std::size_t>(pdim - 1)] + 1;
        pwrseq[static_cast<std::size_t>(pdim - 1)] = 0;
        return;
    }

    int psum = 1;
    k = pdim;
    while (pwrseq[static_cast<std::size_t>(k - 2)] < 1) {
        psum += pwrseq[static_cast<std::size_t>(k - 1)];
        pwrseq[static_cast<std::size_t>(k - 1)] = 0;
        --k;
    }
    pwrseq[static_cast<std::size_t>(k - 1)] += psum;
    --pwrseq[static_cast<std::size_t>(k - 2)];
}

inline double eval_spice2poly(const std::vector<double>& inputs,
                              const std::vector<double>& coefficients,
                              std::vector<double>& partials) {
    const std::size_t num_inputs = inputs.size();
    partials.assign(num_inputs, 0.0);
    if (coefficients.empty())
        return 0.0;
    if (num_inputs == 0)
        return coefficients[0];

    std::vector<int> exponents(num_inputs, 0);
    double sum = coefficients[0];
    for (std::size_t i = 1; i < coefficients.size(); ++i) {
        spice2poly_nxtpwr(exponents);
        double product = 1.0;
        for (std::size_t j = 0; j < num_inputs; ++j)
            product *= spice2poly_evterm(inputs[j], exponents[j]);
        sum += coefficients[i] * product;
    }

    for (std::size_t i = 0; i < num_inputs; ++i) {
        std::fill(exponents.begin(), exponents.end(), 0);
        double partial = 0.0;
        for (std::size_t j = 1; j < coefficients.size(); ++j) {
            spice2poly_nxtpwr(exponents);
            if (exponents[i] == 0)
                continue;

            double product = 1.0;
            for (std::size_t k = 0; k < num_inputs; ++k) {
                if (k != i)
                    product *= spice2poly_evterm(inputs[k], exponents[k]);
                else
                    product *= exponents[k]
                             * spice2poly_evterm(inputs[k], exponents[k] - 1);
            }
            partial += coefficients[j] * product;
        }
        partials[i] = partial;
    }
    return sum;
}

} // namespace neospice
