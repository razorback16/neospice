#pragma once
// Adapter bridging the neospice Device interface to the UCB HFETA (HFET1) code.

#include "devices/device.hpp"
#include "devices/hfet1/hfet1_def.hpp"
#include "devices/hfet1/hfet1_shim.hpp"
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace neospice {

struct HFETAModelCard {
    hfet1::HFETAModel ucb{};   // aggregate UCB model fields

    HFETAModelCard() = default;
    ~HFETAModelCard();

    // Non-copyable / non-movable: UCB model is threaded with raw
    // pointers; copying would alias them.
    HFETAModelCard(const HFETAModelCard&)            = delete;
    HFETAModelCard& operator=(const HFETAModelCard&) = delete;
};

class HFETADevice : public Device {
public:
    struct Geom {
        double length = 1e-6;
        double width  = 20e-6;
        double m      = 1.0;
        bool length_given = false;
        bool width_given  = false;
        bool m_given      = false;
    };

    static std::unique_ptr<HFETADevice> make(
        std::string name, int32_t n_drain, int32_t n_gate, int32_t n_source,
        const Geom& geom, HFETAModelCard& shared_card);

    // Device interface
    void declare_internal_nodes(Circuit& ckt) override;
    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

    int32_t state_vars() const override { return 24; }
    void set_state_ptrs(double* s0, double* s1, double* s2, double* s3, int32_t base) override;
    double compute_trunc(const IntegratorCtx& ctx,
                         const SimOptions& opts) const override;
    bool device_converged() const override;
    std::optional<double> query_param(const std::string& name) const override;
    void reset_temp() override { temp_done_ = false; }

    /// Set initial condition voltages on the underlying UCB instance.
    void set_ic(double vds, bool vds_given, double vgs, bool vgs_given);

    std::vector<NoiseSource> noise_sources(
        double freq, const std::vector<double>& dc_solution) const override;

private:
    explicit HFETADevice(std::string name) : Device(std::move(name)) {}

    mutable hfet1::HFETAInstance inst_{};
    hfet1::HFETAModel* model_ = nullptr;

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
