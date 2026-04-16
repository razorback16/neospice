#include "api/neospice.hpp"
#include "output/raw_writer.hpp"
#include <iostream>
#include <string>
#include <filesystem>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: neospice <netlist.cir> [-o output.raw]\n";
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path;

    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "-o" && i + 1 < argc) {
            output_path = argv[++i];
        }
    }

    if (output_path.empty()) {
        auto p = std::filesystem::path(input_path);
        output_path = p.replace_extension(".raw").string();
    }

    try {
        neospice::Simulator sim;
        auto ckt = sim.load(input_path);
        auto result = sim.run(ckt);

        if (result.transient) {
            neospice::write_raw(output_path, *result.transient);
            std::cout << "Transient results written to " << output_path << "\n";
        } else if (result.dc) {
            neospice::write_raw(output_path, *result.dc);
            std::cout << "DC results written to " << output_path << "\n";
        } else if (result.ac) {
            neospice::write_raw(output_path, *result.ac);
            std::cout << "AC results written to " << output_path << "\n";
        } else {
            std::cerr << "No analysis commands found in netlist\n";
            return 1;
        }
    } catch (const neospice::ParseError& e) {
        std::cerr << "Parse error: " << e.what() << "\n";
        return 1;
    } catch (const neospice::ConvergenceError& e) {
        std::cerr << "Convergence error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
