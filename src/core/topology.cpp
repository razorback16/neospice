#include "core/topology.hpp"
#include "core/circuit.hpp"
#include "devices/vsource.hpp"
#include "devices/isource.hpp"
#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <iostream>

namespace neospice {

namespace {

constexpr int32_t GND_IDX = -1;

struct AdjEntry {
    int32_t node;
    bool is_vsource;
};

using AdjList = std::unordered_map<int32_t, std::vector<AdjEntry>>;

AdjList build_adjacency(const Circuit& ckt) {
    AdjList adj;
    adj[GND_IDX];

    for (const auto& dev : ckt.devices()) {
        auto nodes = dev->external_nodes();
        bool is_vs = (dynamic_cast<const VSource*>(dev.get()) != nullptr);

        for (int32_t n : nodes) {
            if (adj.find(n) == adj.end())
                adj[n] = {};
        }
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            for (std::size_t j = i + 1; j < nodes.size(); ++j) {
                adj[nodes[i]].push_back({nodes[j], is_vs});
                adj[nodes[j]].push_back({nodes[i], is_vs});
            }
        }
    }
    return adj;
}

} // namespace

std::vector<TopologyDiag> check_topology(const Circuit& ckt) {
    std::vector<TopologyDiag> diags;

    auto adj = build_adjacency(ckt);

    // Count connections per node
    std::unordered_map<int32_t, int> connection_count;
    for (const auto& dev : ckt.devices()) {
        auto nodes = dev->external_nodes();
        for (int32_t n : nodes) {
            connection_count[n]++;
        }
    }

    // Check 1: Floating nodes (only one device connection)
    for (const auto& [node, count] : connection_count) {
        if (node == GND_IDX) continue;
        if (count == 1) {
            std::string nname;
            try { nname = ckt.node_name(node); }
            catch (...) { nname = std::to_string(node); }
            diags.push_back({
                TopologyDiag::FLOATING_NODE,
                TopologyDiag::WARNING_SEV,
                "Floating node '" + nname + "' has only one device connection"
            });
        }
    }

    // Check 2: Voltage source loops
    std::unordered_map<int32_t, std::vector<int32_t>> vs_adj;
    for (const auto& dev : ckt.devices()) {
        if (dynamic_cast<const VSource*>(dev.get()) == nullptr) continue;
        auto nodes = dev->external_nodes();
        if (nodes.size() >= 2) {
            vs_adj[nodes[0]].push_back(nodes[1]);
            vs_adj[nodes[1]].push_back(nodes[0]);
        }
    }

    std::unordered_set<int32_t> vs_visited;
    for (auto& [start, neighbors] : vs_adj) {
        if (vs_visited.count(start)) continue;

        std::queue<int32_t> bfs;
        std::unordered_set<int32_t> component;
        bfs.push(start);
        component.insert(start);
        int edges = 0;
        while (!bfs.empty()) {
            int32_t cur = bfs.front(); bfs.pop();
            vs_visited.insert(cur);
            for (int32_t nb : vs_adj[cur]) {
                edges++;
                if (!component.count(nb)) {
                    component.insert(nb);
                    bfs.push(nb);
                }
            }
        }
        edges /= 2;
        if (edges >= static_cast<int>(component.size())) {
            diags.push_back({
                TopologyDiag::VSOURCE_LOOP,
                TopologyDiag::ERROR_SEV,
                "Voltage source loop detected (cycle of voltage sources with no resistance)"
            });
        }
    }

    // Check 3: Disconnected nodes
    std::unordered_set<int32_t> reachable;
    {
        std::queue<int32_t> bfs;
        bfs.push(GND_IDX);
        reachable.insert(GND_IDX);
        while (!bfs.empty()) {
            int32_t cur = bfs.front(); bfs.pop();
            for (const auto& e : adj[cur]) {
                if (!reachable.count(e.node)) {
                    reachable.insert(e.node);
                    bfs.push(e.node);
                }
            }
        }
    }
    for (const auto& [node, neighbors] : adj) {
        if (node == GND_IDX) continue;
        if (!reachable.count(node)) {
            std::string nname;
            try { nname = ckt.node_name(node); }
            catch (...) { nname = std::to_string(node); }
            diags.push_back({
                TopologyDiag::DISCONNECTED,
                TopologyDiag::ERROR_SEV,
                "Node '" + nname + "' is disconnected from ground"
            });
        }
    }

    for (const auto& d : diags) {
        const char* sev = (d.severity == TopologyDiag::ERROR_SEV) ? "ERROR" : "WARNING";
        std::cerr << "[topology " << sev << "] " << d.message << "\n";
    }

    return diags;
}

} // namespace neospice
