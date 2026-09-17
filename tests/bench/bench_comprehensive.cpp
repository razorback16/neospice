// Fixed comprehensive population; paired implementation is shared with other harnesses.
#include "bench/paired_benchmark.hpp"
using namespace neospice::bench::driver;

// All historical analysis workloads remain. Parse and end-to-end are phases
// of each identical workload, rather than separately mislabeled operations.
const std::vector<Workload> workloads = {
    {"dc_ths4131", "ths4131_diff_amp.cir", "op", Workload::DC},
    {"dc_divider", "resistor_divider.cir", "op", Workload::DC},
    {"ac_ths4131_10", "ths4131_diff_amp.cir", "ac dec 10 1 100e6", Workload::AC},
    {"ac_ths4131_1000", "ths4131_diff_amp.cir", "ac dec 1000 1 100e6", Workload::AC, 1000},
    {"ac_rc", "rc_ac.cir", "ac dec 10 1 1e9", Workload::AC, 10, 1, 1e9},
    {"tran_rc", "rc_lowpass.cir", "tran 1e-6 5e-4", Workload::TRAN, 0, 1e-6, 5e-4},
    {"tran_rlc", "rlc_series.cir", "tran 1e-7 1e-4", Workload::TRAN, 0, 1e-7, 1e-4},
    {"tran_pulse", "pulse_defaults.cir", "tran 1e-7 1e-4", Workload::TRAN, 0, 1e-7, 1e-4},
    {"noise_divider", "resistor_divider_noise.cir", "noise v(out) v1 dec 10 1 1e9", Workload::NOISE, 10, 1, 1e9},
    {"sweep_divider", "resistor_divider.cir", "dc v1 -5 5 0.01", Workload::SWEEP, 1001, -5, 5},
    {"op_ac_ths4131", "ths4131_diff_amp.cir", "op; ac dec 10 1 100e6", Workload::OP_AC},
    {"op_ac_opa1632", "opa1632_test.cir", "op; ac dec 10 1 100e6", Workload::OP_AC},
    // Fixed generated population; see tools/generate_paired_circuits.py.
    {"dc_mesh_10", "paired/mesh_10.cir", "op", Workload::DC},
    {"dc_mesh_32", "paired/mesh_32.cir", "op", Workload::DC},
    {"dc_mesh_71", "paired/mesh_71.cir", "op", Workload::DC},
    {"dc_mesh_141", "paired/mesh_141.cir", "op", Workload::DC},
    {"dc_diode_ladder_100", "paired/diode_ladder_100.cir", "op", Workload::DC},
    {"dc_diode_ladder_1000", "paired/diode_ladder_1000.cir", "op", Workload::DC},
    {"dc_diode_ladder_5000", "paired/diode_ladder_5000.cir", "op", Workload::DC},
    {"dc_diode_ladder_20000", "paired/diode_ladder_20000.cir", "op", Workload::DC},
    {"ac_rc_grid_8", "paired/rc_grid_8.cir", "ac dec 10 1 1e6", Workload::AC, 10, 1, 1e6},
    {"tran_rc_grid_8", "paired/rc_grid_8.cir", "tran 1e-6 5e-4", Workload::TRAN, 0, 1e-6, 5e-4},
    {"ac_rc_grid_24", "paired/rc_grid_24.cir", "ac dec 10 1 1e6", Workload::AC, 10, 1, 1e6},
    {"tran_rc_grid_24", "paired/rc_grid_24.cir", "tran 1e-6 5e-4", Workload::TRAN, 0, 1e-6, 5e-4},
    {"ac_rc_grid_48", "paired/rc_grid_48.cir", "ac dec 10 1 1e6", Workload::AC, 10, 1, 1e6},
    {"tran_rc_grid_48", "paired/rc_grid_48.cir", "tran 1e-6 5e-4", Workload::TRAN, 0, 1e-6, 5e-4},
    {"noise_rc_grid_24", "paired/rc_grid_24.cir", "noise v(out) v1 dec 10 1 1e6", Workload::NOISE, 10, 1, 1e6},
    {"ac_diode_rc_grid_8", "paired/diode_rc_grid_8.cir", "ac dec 10 1 1e6", Workload::AC, 10, 1, 1e6},
    {"tran_diode_rc_grid_8", "paired/diode_rc_grid_8.cir", "tran 1e-6 5e-4", Workload::TRAN, 0, 1e-6, 5e-4},
    {"ac_diode_rc_grid_24", "paired/diode_rc_grid_24.cir", "ac dec 10 1 1e6", Workload::AC, 10, 1, 1e6},
    {"tran_diode_rc_grid_24", "paired/diode_rc_grid_24.cir", "tran 1e-6 5e-4", Workload::TRAN, 0, 1e-6, 5e-4},
    {"ac_diode_rc_grid_48", "paired/diode_rc_grid_48.cir", "ac dec 10 1 1e6", Workload::AC, 10, 1, 1e6},
    {"tran_diode_rc_grid_48", "paired/diode_rc_grid_48.cir", "tran 1e-6 5e-4", Workload::TRAN, 0, 1e-6, 5e-4},
    {"noise_diode_rc_grid_24", "paired/diode_rc_grid_24.cir", "noise v(out) v1 dec 10 1 1e6", Workload::NOISE, 10, 1, 1e6},
};

int main(int argc, char** argv) { return run(workloads, argc, argv); }
