#pragma once
#include "core/types.hpp"
#include "core/matrix.hpp"
#include "devices/device.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cudaspice {

struct AnalysisCommand {
    enum Type { OP, TRAN, AC, DC_SWEEP };
    Type type;
    double tran_tstep = 0, tran_tstop = 0;
    enum ACMode { DEC, OCT, LIN };
    ACMode ac_mode = DEC;
    int ac_npoints = 10;
    double ac_fstart = 1.0, ac_fstop = 1e6;
};

class Circuit {
public:
    /// Map node name to internal index. "0", "gnd", "GND" → GROUND_INTERNAL (-1)
    int32_t node(const std::string& name);

    int32_t num_nodes() const { return next_node_; }
    int32_t num_vars() const  { return num_vars_; }

    void add_device(std::unique_ptr<Device> dev);

    /// Assign branch indices, build sparsity pattern, assign offsets.
    void finalize();

    const SparsityPattern& pattern() const { return *pattern_; }
    const std::vector<std::unique_ptr<Device>>& devices() const { return devices_; }
    std::vector<std::unique_ptr<Device>>& devices()             { return devices_; }

    std::string node_name(int32_t idx) const;
    int32_t     node_index(const std::string& name) const;

    SimOptions                          options;
    std::vector<AnalysisCommand>        analyses;
    std::unordered_map<int32_t, double> ic;       // .ic
    std::unordered_map<int32_t, double> nodeset;  // .nodeset
    std::vector<std::string>            save_signals;

private:
    std::vector<std::unique_ptr<Device>> devices_;
    std::unordered_map<std::string, int32_t> node_map_;
    std::vector<std::string>                 node_names_;
    int32_t next_node_ = 0;
    int32_t num_vars_  = 0;
    std::unique_ptr<SparsityPattern> pattern_;
};

} // namespace cudaspice
