// THS4131's original circuit and DEC sweep densities, with validated paired phases.
#include "bench/paired_benchmark.hpp"
using namespace neospice::bench::driver;

// Load/cleanup/total are recorded phases of every workload. The historical
// parse-only timer is no longer a way to accept an unvalidated circuit.
// All density/range combinations from the old harness are retained.
const std::vector<Workload> workloads = {
    {"dc_ths4131", "ths4131_diff_amp.cir", "op", Workload::DC},
    {"ac_ths4131_10", "ths4131_diff_amp.cir", "ac dec 10 1 100e6", Workload::AC},
    {"op_ac_ths4131_10", "ths4131_diff_amp.cir", "op; ac dec 10 1 100e6", Workload::OP_AC},
    {"op_ac_ths4131_100", "ths4131_diff_amp.cir", "op; ac dec 100 1 100e6", Workload::OP_AC, 100},
    {"op_ac_ths4131_1000", "ths4131_diff_amp.cir", "op; ac dec 1000 1 100e6", Workload::OP_AC, 1000},
};

int main(int argc, char** argv) { return run(workloads, argc, argv); }
