#include "api/neospice.hpp"
#include <cstdio>
#include <cmath>
#include <string>

using namespace neospice;

int main() {
    const std::string circuits = std::string(TEST_CIRCUITS_DIR);
    const std::string cir = circuits + "/tlv3201_switching.cir";

    Simulator sim;

    auto ckt = sim.load(cir);
    ckt.options.verbose = true;
    printf("Loaded circuit, running...\n");
    auto result = sim.run(ckt);

    if (std::holds_alternative<TransientResult>(result.analysis)) {
        auto& tr = std::get<TransientResult>(result.analysis);
        printf("\nTime points: %zu\n", tr.time.size());

        // Print available signals
        printf("Voltage signals: ");
        for (auto& [k,v] : tr.voltages)
            printf("%s ", k.c_str());
        printf("\n");

        // Print v(out) at a few time points
        if (tr.voltages.count("v(out)")) {
            auto& vout = tr.voltages.at("v(out)");
            printf("\nv(out) values:\n");
            for (size_t i = 0; i < tr.time.size() && i < 20; ++i)
                printf("  t=%11.4e  v(out)=%12.6g\n", tr.time[i], vout[i]);
            if (tr.time.size() > 20) {
                printf("  ...\n");
                for (size_t i = tr.time.size()-5; i < tr.time.size(); ++i)
                    printf("  t=%11.4e  v(out)=%12.6g\n", tr.time[i], vout[i]);
            }
        }

        // Print v(inp) and v(inm) at key points
        if (tr.voltages.count("v(inp)") && tr.voltages.count("v(inm)")) {
            auto& vinp = tr.voltages.at("v(inp)");
            auto& vinm = tr.voltages.at("v(inm)");
            printf("\nInput signals at key points:\n");
            for (size_t i = 0; i < tr.time.size() && i < 20; ++i)
                printf("  t=%11.4e  v(inp)=%12.6g  v(inm)=%12.6g  diff=%12.6g\n",
                       tr.time[i], vinp[i], vinm[i], vinp[i] - vinm[i]);
        }

        // Check all signals for extreme values
        printf("\nSignals with extreme values (>100V):\n");
        for (auto& [k,v] : tr.voltages) {
            double maxv = 0;
            size_t max_idx = 0;
            for (size_t i = 0; i < v.size(); ++i) {
                if (std::abs(v[i]) > std::abs(maxv)) {
                    maxv = v[i];
                    max_idx = i;
                }
            }
            if (std::abs(maxv) > 100.0)
                printf("  %-25s max=%.6g at t=%.4e\n", k.c_str(), maxv, tr.time[max_idx]);
        }

        // Print key internal signals
        const char* key_signals[] = {
            "v(x1.36)", "v(x1.108)", "v(x1.109)", "v(x1.84)",
            "v(x1.107)", "v(x1.110)", "v(x1.112)", "v(x1.6)",
            "v(x1.7)", "v(x1.30)", "v(x1.17)"
        };
        for (auto sig_name : key_signals) {
            if (tr.voltages.count(sig_name)) {
                auto& v = tr.voltages.at(sig_name);
                printf("\n%s values:\n", sig_name);
                for (size_t i = 0; i < tr.time.size() && i < 10; ++i)
                    printf("  t=%11.4e  %s=%12.6g\n", tr.time[i], sig_name, v[i]);
                if (tr.time.size() > 10) {
                    printf("  ...\n");
                    // Print around 1us (when pulse starts)
                    for (size_t i = 0; i < tr.time.size(); ++i) {
                        if (tr.time[i] >= 0.9e-6 && tr.time[i] <= 1.2e-6) {
                            printf("  t=%11.4e  %s=%12.6g\n", tr.time[i], sig_name, v[i]);
                        }
                    }
                    printf("  ...\n");
                    for (size_t i = tr.time.size()-3; i < tr.time.size(); ++i)
                        printf("  t=%11.4e  %s=%12.6g\n", tr.time[i], sig_name, v[i]);
                }
            }
        }
    }
    return 0;
}
