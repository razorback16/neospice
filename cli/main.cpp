#include "api/neospice.hpp"
#include "output/raw_writer.hpp"
#include <iostream>
#include <string>
#include <filesystem>
#include <chrono>

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
        using Clock = std::chrono::high_resolution_clock;
        neospice::Simulator sim;
        auto t_parse = Clock::now();
        auto ckt = sim.load(input_path);
        auto t_loaded = Clock::now();
        auto result = sim.run(ckt);
        auto t_sim = Clock::now();

        int written = 0;
        if (result.dc) {
            auto dc_path = output_path;
            if (result.ac || result.transient)
                dc_path = std::filesystem::path(output_path).replace_extension(".dc.raw").string();
            neospice::write_raw(dc_path, *result.dc);
            std::cout << "DC results written to " << dc_path << "\n";
            ++written;
        }
        if (result.ac) {
            auto ac_path = output_path;
            if (result.dc || result.transient)
                ac_path = std::filesystem::path(output_path).replace_extension(".ac.raw").string();
            neospice::write_raw(ac_path, *result.ac);
            std::cout << "AC results written to " << ac_path << "\n";
            ++written;
        }
        if (result.transient) {
            auto tran_path = output_path;
            if (result.dc || result.ac)
                tran_path = std::filesystem::path(output_path).replace_extension(".tran.raw").string();
            neospice::write_raw(tran_path, *result.transient);
            std::cout << "Transient results written to " << tran_path << "\n";
            ++written;
        }
        if (written == 0) {
            std::cerr << "No analysis commands found in netlist\n";
            return 1;
        }
        auto t_write = Clock::now();
        auto parse_us = std::chrono::duration<double, std::micro>(t_loaded - t_parse).count();
        auto sim_us = std::chrono::duration<double, std::micro>(t_sim - t_loaded).count();
        auto write_us = std::chrono::duration<double, std::micro>(t_write - t_sim).count();
        auto total_us = std::chrono::duration<double, std::micro>(t_write - t_parse).count();
        std::cerr << "[neospice] parse=" << parse_us/1000.0 << " ms, sim="
                  << sim_us/1000.0 << " ms, write=" << write_us/1000.0
                  << " ms, total=" << total_us/1000.0 << " ms\n";
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
