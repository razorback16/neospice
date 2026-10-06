// Compare browser gallery results with the existing ngspice 47 comparators.
// Input is a whitespace-delimited export written by web/scripts/check-gallery.ts.
#include "framework/comparator.hpp"
#include "framework/ngspice_runner.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
using namespace neospice;

template<class Map> void read_real(std::istream& in, Map& out, size_t signals, size_t points) {
    for (size_t i = 0; i < signals; ++i) {
        std::string name; in >> name;
        auto& values = out[name]; values.resize(points);
        for (double& value : values) in >> value;
    }
}
template<class Map> void read_complex(std::istream& in, Map& out, size_t signals, size_t points) {
    for (size_t i = 0; i < signals; ++i) {
        std::string name; in >> name;
        auto& values = out[name]; values.resize(points);
        for (auto& value : values) { double re, im; in >> re >> im; value = {re, im}; }
    }
}
int main(int argc, char** argv) {
    if (argc != 4) { std::cerr << "Usage: gallery_reference netlist actual.txt example-id\n"; return 2; }
    try {
        std::ifstream in(argv[2]); in.exceptions(std::ios::failbit | std::ios::badbit);
        std::string mode; size_t count, nv, ni; in >> mode >> count >> nv >> ni;
        NgspiceRunner reference;
        CompareResult comparison;
        const std::string id = argv[3];
        if (mode == "ac") {
            ACResult actual; actual.status.converged = true; actual.frequency.resize(count);
            for (auto& f : actual.frequency) in >> f;
            read_complex(in, actual.voltages, nv, count); read_complex(in, actual.currents, ni, count);
            const auto expected = reference.run_ac(argv[1]);
            comparison = compare_ac(expected, actual, {1e-3, 1e-6});
        } else if (mode == "dc") {
            DCResult actual; actual.status.converged = true;
            for (size_t i=0;i<nv;++i) {std::string key;double v;in>>key>>v;actual.node_voltages[key]=v;}
            for (size_t i=0;i<ni;++i) {std::string key;double v;in>>key>>v;actual.branch_currents[key]=v;}
            comparison = compare_dc(reference.run_dc(argv[1]), actual, {1e-3, 1e-6});
        } else if (mode == "transient") {
            TransientResult actual; actual.status.converged = true; actual.time.resize(count);
            for (auto& t : actual.time) in >> t;
            read_real(in, actual.voltages, nv, count); read_real(in, actual.currents, ni, count);
            const auto expected = reference.run_transient(argv[1]);
            if (id == "ring-oscillator") {
                // Existing RingOscillator5Stage contract, unchanged.
                comparison = compare_transient_oscillator(expected, actual, {2e-4, 1e-3, 5e-2, 5e-2, 3});
            } else if (id == "cmos-inverter") {
                const auto invalid = validate_transient_data(expected, actual);
                if (!invalid.empty()) throw std::runtime_error(invalid);
                const auto ng = extract_edges(expected.time, expected.voltages.at("v(out)"), 0, 1.8, 1e-9);
                const auto neo = extract_edges(actual.time, actual.voltages.at("v(out)"), 0, 1.8, 1e-9);
                if (ng.empty() || ng.size()!=neo.size()) throw std::runtime_error("Missing or mismatched inverter edges");
                // Existing CMOSInverterTransient contract, unchanged.
                const auto edges = compare_edges(ng, neo, {1e-3, 2e-2, 1e-3, 7e-3});
                std::cout << id << ": " << (edges.passed?"PASS":"FAIL") << " " << edges.detail << '\n';
                return edges.passed?0:1;
            } else {
                Tolerance tolerance{1e-3, 1e-3};
                if (id=="rc-filter") tolerance={3e-5, 3e-5};
                if (id=="rlc-resonator") tolerance={2e-5, 2e-5};
                comparison=compare_transient(expected, actual, tolerance);
            }
        } else throw std::runtime_error("Unknown analysis mode");
        std::cout << std::setprecision(12) << id << ": " << (comparison.passed?"PASS":"FAIL")
                  << " worst=" << comparison.worst_signal << " error=" << comparison.worst_error << '\n';
        if (!comparison.passed) for (const auto& s : comparison.signals)
            std::cout << s.name << " absolute=" << s.max_absolute_error << " relative=" << s.max_normalized_error
                      << " at=" << s.worst_coordinate << " reference=" << s.reference_at_worst << " wasm=" << s.actual_at_worst << '\n';
        return comparison.passed?0:1;
    } catch (const std::exception& e) {std::cerr << e.what() << '\n';return 1;}
}
