#pragma once
// Adapter bridging the neospice Device interface to the UCB VDMOS code.

#include "devices/device.hpp"
#include "devices/vdmos/vdmos_def.hpp"
#include "devices/vdmos/vdmos_shim.hpp"
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace neospice {

struct VDMOSModelCard {
    vdmos::VDMOSModel ucb{};   // aggregate UCB model fields

    VDMOSModelCard() = default;
    ~VDMOSModelCard();

    // Non-copyable / non-movable: UCB model is threaded with raw
    // pointers; copying would alias them.
    VDMOSModelCard(const VDMOSModelCard&)            = delete;
    VDMOSModelCard& operator=(const VDMOSModelCard&) = delete;
};

class VDMOSDevice : public Device {
public:
    struct Geom {
        double M = 1.0;
    };

    static std::unique_ptr<VDMOSDevice> make(
        std::string name, int32_t n_d, int32_t n_g, int32_t n_s, int32_t n_tj, int32_t n_tc,
        const Geom& geom, VDMOSModelCard& shared_card);

    // Device interface
    void declare_internal_nodes(Circuit& ckt) override;
    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

    int32_t state_vars() const override { return 18; }
    void set_state_ptrs(double* s0, double* s1, double* s2, double* s3, int32_t base) override;
    double compute_trunc(const IntegratorCtx& ctx,
                         const SimOptions& opts) const override;
    bool device_converged() const override;
    std::optional<double> query_param(const std::string& name) const override;
    void reset_temp() override { temp_done_ = false; }

    std::vector<NoiseSource> noise_sources(
        double freq, const std::vector<double>& dc_solution) const override;

private:
    explicit VDMOSDevice(std::string name) : Device(std::move(name)) {}

    mutable vdmos::VDMOSInstance inst_{};
    vdmos::VDMOSModel* model_ = nullptr;

    std::vector<std::pair<int,int>> journal_;

    double* state0_ = nullptr;
    double* state1_ = nullptr;
    double* state2_ = nullptr;
    double* state3_ = nullptr;
    int32_t state_base_ = -1;

    mutable bool temp_done_ = false;
    mutable int last_noncon_ = 0;

    int32_t max_neo_node_ = -1;

    std::vector<double> ghost_voltages_;
    std::vector<double> ghost_rhs_;
};

} // namespace neospice
