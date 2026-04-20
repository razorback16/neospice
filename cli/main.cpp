#include "api/neospice.hpp"
#include "output/raw_writer.hpp"
#include <iostream>
#include <string>
#include <filesystem>
#include <chrono>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: neospice <netlist.cir> [-o output.raw] [--split]\n";
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path;
    bool split = false;

    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "-o" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (std::string(argv[i]) == "--split") {
            split = true;
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

        bool has_any = result.dc || result.ac || result.transient || result.dc_sweep;
        if (!has_any) {
            std::cerr << "No analysis commands found in netlist\n";
            return 1;
        }

        if (split) {
            int written = 0;
            bool multi = (!!result.dc + !!result.ac + !!result.transient + !!result.dc_sweep) > 1;
            if (result.dc) {
                auto path = multi
                    ? std::filesystem::path(output_path).replace_extension(".dc.raw").string()
                    : output_path;
                neospice::write_raw(path, *result.dc, ckt.title);
                std::cout << "DC results written to " << path << "\n";
                ++written;
            }
            if (result.ac) {
                auto path = multi
                    ? std::filesystem::path(output_path).replace_extension(".ac.raw").string()
                    : output_path;
                neospice::write_raw(path, *result.ac, ckt.title);
                std::cout << "AC results written to " << path << "\n";
                ++written;
            }
            if (result.transient) {
                auto path = multi
                    ? std::filesystem::path(output_path).replace_extension(".tran.raw").string()
                    : output_path;
                neospice::write_raw(path, *result.transient, ckt.title);
                std::cout << "Transient results written to " << path << "\n";
                ++written;
            }
            if (result.dc_sweep) {
                auto path = multi
                    ? std::filesystem::path(output_path).replace_extension(".dcsweep.raw").string()
                    : output_path;
                neospice::write_raw(path, *result.dc_sweep, ckt.title);
                std::cout << "DC sweep results written to " << path << "\n";
                ++written;
            }
        } else {
            neospice::write_raw(output_path, result, ckt.title);
            std::cout << "Results written to " << output_path << "\n";
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
