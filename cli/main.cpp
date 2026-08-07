#include "api/neospice.hpp"
#include "output/raw_writer.hpp"
#include <iostream>
#include <string>
#include <filesystem>
#include <chrono>
#include <optional>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: neospice <netlist.cir> [-o output.raw] [--split] "
                     "[-D ngbehavior=<mode>]\n";
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path;
    bool split = false;
    std::optional<bool> pspice_compat;  // -D ngbehavior=... override

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "--split") {
            split = true;
        } else if (arg == "-D" && i + 1 < argc) {
            // Mirror ngspice's `-D ngbehavior=<mode>`: ps/lt/psa/a/all engage
            // PSpice/LTspice compatibility; ng/spice/spice3 disable it.
            std::string kv = argv[++i];
            auto eq = kv.find('=');
            std::string key = kv.substr(0, eq);
            std::string val = eq == std::string::npos ? "" : kv.substr(eq + 1);
            if (key == "ngbehavior") {
                pspice_compat = (val == "ps" || val == "lt" || val == "psa" ||
                                 val == "a" || val == "all");
            }
        }
    }

    if (output_path.empty()) {
        auto p = std::filesystem::path(input_path);
        output_path = p.replace_extension(".raw").string();
    }

    try {
        using Clock = std::chrono::high_resolution_clock;
        neospice::Simulator sim;
        sim.set_pspice_compat(pspice_compat);
        auto t_parse = Clock::now();
        auto ckt = sim.load(input_path);
        auto t_loaded = Clock::now();
        auto result = sim.run(ckt);
        auto t_sim = Clock::now();

        if (std::holds_alternative<std::monostate>(result.analysis) && !result.step) {
            std::cerr << "No analysis commands found in netlist\n";
            return 1;
        }

        for (const auto& po : result.print_output) {
            std::cout << po;
        }
        // Print output from .step sub-results
        if (result.step) {
            for (size_t i = 0; i < result.step->results.size(); ++i) {
                for (const auto& po : result.step->results[i].print_output) {
                    std::cout << po;
                }
            }
        }

        if (split) {
            std::visit(neospice::overloaded{
                [](std::monostate) {},
                [&](const neospice::DCResult& dc) {
                    neospice::write_raw(output_path, dc, ckt.title);
                    std::cout << "DC results written to " << output_path << "\n";
                },
                [&](const neospice::ACResult& ac) {
                    auto path = std::filesystem::path(output_path)
                        .replace_extension(".ac.raw").string();
                    neospice::write_raw(path, ac, ckt.title);
                    std::cout << "AC results written to " << path << "\n";
                },
                [&](const neospice::TransientResult& tran) {
                    auto path = std::filesystem::path(output_path)
                        .replace_extension(".tran.raw").string();
                    neospice::write_raw(path, tran, ckt.title);
                    std::cout << "Transient results written to " << path << "\n";
                },
                [&](const neospice::DCSweepResult& sw) {
                    auto path = std::filesystem::path(output_path)
                        .replace_extension(".dcsweep.raw").string();
                    neospice::write_raw(path, sw, ckt.title);
                    std::cout << "DC sweep results written to " << path << "\n";
                },
                [](const auto&) {
                    // NoiseResult, TFResult, SensResult, PZResult — no raw writer
                }
            }, result.analysis);
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
    } catch (const neospice::SimulationError& e) {
        const auto& status = e.status();
        std::cerr << "Simulation error: " << e.what() << "\n";
        std::cerr << "  converged: " << (status.converged ? "true" : "false") << "\n";
        std::cerr << "  iterations: " << status.iterations << "\n";
        std::cerr << "  residual: " << status.residual << "\n";
        std::cerr << "  worst_node_idx: " << status.worst_node_idx << "\n";
        std::cerr << "  method: "
                  << neospice::convergence_method_name(status.convergence_method) << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
