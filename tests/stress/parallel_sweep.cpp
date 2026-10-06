#include "api/neospice.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

// Standalone race-detector harness: no reference simulator or test framework.
// Every point varies temperature to exercise setup and preprocessing state.
int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: neospice_sweep_stress <tests/circuits>\n";
        return 2;
    }
    try {
        const std::vector<std::string> files = {
            "mos1_nmos_dc_op.cir", "mos2_nmos_dc_op.cir", "mos3_nmos_dc_op.cir",
            "mos9_nmos_dc_op.cir", "bsim3v32_nmos_dc_op.cir", "bsimsoi_nmos_dc_op.cir",
            "hisim2_nmos_dc_op.cir", "hisimhv_nmos_dc_op.cir", "vdmos_nmos_dc_op.cir",
            "vbic_npn_dc.cir", "jfet2_njf_dc.cir", "bjt_off_bias.cir",
            "hfet1_nfet_dc_op.cir", "hfet2_nfet_dc_op.cir", "mes_dc_op.cir"};
        neospice::Simulator sim;
        std::vector<neospice::SweepPoint> points(32);
        for (std::size_t i = 0; i < points.size(); ++i)
            points[i].temperature_celsius = 20.0 + i;
        const auto check = [&](const std::string& file, const std::string& input, bool from_file) {
            auto serial = sim.run_sweep(input, points, {.workers=1}, from_file);
            auto parallel = sim.run_sweep(input, points, {.workers=8}, from_file);
            for (std::size_t i = 0; i < points.size(); ++i) {
                if (!serial.samples[i].error.empty() || !parallel.samples[i].error.empty())
                    throw std::runtime_error(file + ": " + serial.samples[i].error + parallel.samples[i].error);
                const auto& a = std::get<neospice::DCResult>(serial.samples[i].result->analysis);
                const auto& b = std::get<neospice::DCResult>(parallel.samples[i].result->analysis);
                if (a.node_voltages != b.node_voltages || a.branch_currents != b.branch_currents)
                    throw std::runtime_error(file + ": serial/parallel results differ");
            }
            std::cout << file << ": 32 serial/parallel points agree\n";
        };
        for (const auto& file : files)
            check(file, (std::filesystem::path(argv[1]) / file).string(), true);
        for (int level : {49, 14}) {
            const auto name = "MOS level " + std::to_string(level);
            check(name, "MOS\nVdd vdd 0 1.8\nVgs gate 0 0.9\nRd vdd drain 1k\n"
                  "M1 drain gate 0 0 model W=10u L=1u\n.model model NMOS LEVEL=" +
                  std::to_string(level) + "\n.op\n.end\n", false);
        }
        check("diode", "Diode\nV1 in 0 1\nR1 in out 1k\nD1 out 0 dm\n.model dm D(IS=1e-14 RS=10)\n.op\n.end\n", false);
        check("JFET", "JFET\nV1 in 0 5\nR1 in out 1k\nJ1 out 0 0 jm\n.model jm NJF(VTO=-2 BETA=1m)\n.op\n.end\n", false);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
