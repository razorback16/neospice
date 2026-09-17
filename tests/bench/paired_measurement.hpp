#pragma once
#include <chrono>
#include <cmath>
#include <limits>
#include <vector>
#include <exception>
#include <tuple>
#include <optional>
#include <stdexcept>
#include <utility>

namespace neospice::bench {

// Repeated multiplication used by DEC grids accumulates rounding at each
// point. Bound request-endpoint roundoff by grid length; this is independent
// of (and does not change) the waveform comparator's numerical tolerances.
inline bool complete_frequency_request(const std::vector<double>& axis,
                                       double start, double stop, size_t count) {
    if (count < 2 || axis.size() != count || !(start > 0) || !(stop > start) ||
        !std::isfinite(start) || !std::isfinite(stop)) return false;
    for (size_t i = 0; i < count; ++i)
        if (!std::isfinite(axis[i]) || axis[i] <= 0 ||
            (i && axis[i] <= axis[i-1])) return false;
    const double bound = 4 * count * std::numeric_limits<double>::epsilon();
    return std::abs(axis.front()-start) <= bound * start &&
           std::abs(axis.back()-stop) <= bound * stop;
}

struct Phases {
    double load_us = 0;
    double analysis_us = 0;
    double cleanup_us = 0;
    double total_us = 0;
};

// Results remain alive for validation outside every timer. Circuit/plot cleanup
// is inside total_us for both implementations; application result destruction
// is outside for both. Analysis includes setup deferred by each public API and
// copying its results, so it must never be described as solve-only time.
template<class Load, class Analyze, class Cleanup>
auto measure(Load load, Analyze analyze, Cleanup cleanup) {
    using Clock = std::chrono::steady_clock;
    using Result = decltype(analyze());
    std::optional<Result> result;
    const auto start = Clock::now();
    try {
        load();
        const auto loaded = Clock::now();
        result.emplace(analyze());
        const auto analyzed = Clock::now();
        cleanup();
        const auto cleaned = Clock::now();
        auto us = [](auto duration) {
            return std::chrono::duration<double, std::micro>(duration).count();
        };
        return std::pair{std::move(*result), Phases{
            us(loaded-start), us(analyzed-loaded), us(cleaned-analyzed), us(cleaned-start)}};
    } catch (...) {
        // Preserve the original failure if cleanup also fails.
        try { cleanup(); } catch (...) {}
        throw;
    }
}

// Invoke both implementations even when one fails. Callers retain diagnostics
// and must reject the entire pair if either optional is empty or validation
// fails. There is no timing-only path that can bypass the validation callback.
template<class Neo, class Reference, class Validate>
auto paired(bool neo_first, Neo neo, Reference reference, Validate validate) {
    using N = decltype(neo());
    using R = decltype(reference());
    std::optional<N> n;
    std::optional<R> r;
    std::exception_ptr error;
    auto call = [&](auto& destination, auto& fn) {
        try { destination.emplace(fn()); }
        catch (...) { if (!error) error = std::current_exception(); }
    };
    if (neo_first) { call(n, neo); call(r, reference); }
    else { call(r, reference); call(n, neo); }
    if (error) std::rethrow_exception(error);
    auto validation = validate(r->first, n->first);
    if (!validation.passed || validation.num_points_compared <= 0)
        throw std::runtime_error("Output validation failed: " + validation.worst_signal);
    return std::tuple{n->second, r->second, std::move(validation)};
}

} // namespace neospice::bench
