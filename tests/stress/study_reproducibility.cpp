// Reproducibility check for neospice parallel studies.
// For each workload: Monte Carlo with several worker counts and repeats.
// Every sample's full result (all node waveforms) is hashed bitwise.
// Optionally each sample is re-simulated standalone and compared bitwise.
//
// Usage: neospice_study_repro <netlist> <param|-> <nominal> <sigma> <samples> [standalone]
// "-" draws no parameters; randomness then comes only from netlist
// expressions such as agauss(). Example netlists are in tests/stress/repro.
#include "api/neospice.hpp"
#include "parser/netlist_parser.hpp"
#include <algorithm>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <variant>
#include <vector>
using namespace neospice;

struct Hash {
    std::uint64_t h = 1469598103934665603ull;
    void bytes(const void* p, std::size_t n) {
        auto* c = static_cast<const unsigned char*>(p);
        for (std::size_t i = 0; i < n; ++i) { h ^= c[i]; h *= 1099511628211ull; }
    }
    template <class T> void vec(const std::vector<T>& v) { bytes(v.data(), v.size() * sizeof(T)); }
};

static void hash_result(Hash& h, const SimulationResult& r) {
    if (auto* dc = std::get_if<DCResult>(&r.analysis)) h.vec(dc->node_voltages_dense);
    else if (auto* ac = std::get_if<ACResult>(&r.analysis)) {
        h.vec(ac->frequency);
        for (auto& v : ac->voltages_dense) h.vec(v);
    } else if (auto* tr = std::get_if<TransientResult>(&r.analysis)) {
        h.vec(tr->time);
        for (auto& v : tr->voltages_dense) h.vec(v);
    }
}

static std::uint64_t hash_sample(const SweepSample& s) {
    Hash h;
    for (auto& [k, v] : s.point.parameters) { h.bytes(k.data(), k.size()); h.bytes(&v, sizeof v); }
    if (!s.error.empty()) h.bytes(s.error.data(), s.error.size());
    else hash_result(h, *s.result);
    return h.h;
}

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr, "usage: %s netlist param|- nominal sigma samples [standalone]\n", argv[0]);
        return 2;
    }
    std::string file = argv[1], param = argv[2];
    double nominal = std::atof(argv[3]), sigma = std::atof(argv[4]);
    std::size_t n = std::atoi(argv[5]);
    bool standalone = argc > 6 && std::string(argv[6]) == "standalone";
    const unsigned worker_counts[] = {1, 2, 4, 8, 16, 24};
    std::vector<std::uint64_t> reference;
    int batches = 0, identical_batches = 0, failed = 0;
    Simulator sim;
    for (unsigned w : worker_counts)
        for (int rep = 0; rep < 2; ++rep) {
            MonteCarloOptions o;
            o.samples = n;
            o.execution.workers = w;
            o.execution.seed = 20261014;
            // "-" means no sampled parameters: randomness comes only from
            // netlist expressions such as agauss(), seeded per job.
            auto batch = param == "-"
                ? sim.run_sweep(file, std::vector<SweepPoint>(n), o.execution, true)
                : sim.monte_carlo(file, {{param, nominal, sigma, VariationDistribution::Gaussian}},
                                  o, true);
            std::vector<std::uint64_t> hashes;
            for (auto& s : batch.samples) hashes.push_back(hash_sample(s));
            ++batches;
            if (reference.empty()) {
                reference = hashes;
                std::vector<std::uint64_t> sorted = hashes;
                std::sort(sorted.begin(), sorted.end());
                std::printf("  distinct sample results: %zu of %zu\n",
                            std::size_t(std::unique(sorted.begin(), sorted.end()) - sorted.begin()), n);
                for (auto& s : batch.samples) failed += !s.error.empty();
                ++identical_batches;
                if (standalone) {
                    // Each sample alone: fresh parser, same parameter values.
                    int mismatch = 0;
                    for (std::size_t i = 0; i < n; ++i) {
                        NetlistParser p;
                        p.set_parameter_overrides(std::map<std::string, double>(
                            batch.samples[i].point.parameters.begin(),
                            batch.samples[i].point.parameters.end()));
                        auto c = p.parse_file(file);
                        SweepSample alone;
                        alone.point = batch.samples[i].point;
                        try { alone.result = sim.run(c); }
                        catch (const std::exception& e) { alone.error = e.what(); }
                        if (alone.error.empty() != batch.samples[i].error.empty() ||
                            (alone.error.empty() && hash_sample(alone) != hashes[i]))
                            ++mismatch;
                    }
                    std::printf("  standalone re-simulation mismatches: %d of %zu\n", mismatch, n);
                }
            } else if (hashes == reference) {
                ++identical_batches;
            }
            std::printf("  workers=%-2u rep=%d used=%u identical=%s\n", w, rep, batch.workers_used,
                        hashes == reference ? "yes" : "NO");
        }
    std::printf("{\"file\": \"%s\", \"samples\": %zu, \"failed_samples\": %d, \"batches\": %d, "
                "\"bit_identical_batches\": %d}\n", file.c_str(), n, failed, batches, identical_batches);
}
