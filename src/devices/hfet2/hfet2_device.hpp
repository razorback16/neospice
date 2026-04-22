#pragma once
// Adapter bridging the neospice Device interface to the UCB HFET2 code.

#include "devices/device.hpp"
#include "devices/hfet2/hfet2_def.hpp"
#include "devices/hfet2/hfet2_shim.hpp"
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace neospice {

struct HFET2ModelCard {
    hfet2::HFET2Model ucb{};   // aggregate UCB model fields

    HFET2ModelCard() = default;
    ~HFET2ModelCard();

    // Non-copyable / non-movable: UCB model is threaded with raw
    // pointers; copying would alias them.
    HFET2ModelCard(const HFET2ModelCard&)            = delete;
    HFET2ModelCard& operator=(const HFET2ModelCard&) = delete;
};

class HFET2Device : public Device {
public:
    struct Geom {
        double L = 1e-6;
        double W = 20e-6;
        double M = 1.0;
    };

    static std::unique_ptr<HFET2Device> make(
        std::string name, int32_t n_drain, int32_t n_gate, int32_t n_source,
        const Geom& geom, HFET2ModelCard& shared_card);

    // Device interface
    void declare_internal_nodes(Circuit& ckt) override;
    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

    int32_t state_vars() const override { return 13; }
    void set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) override;
    double compute_trunc(const IntegratorCtx& ctx,
                         const SimOptions& opts) const override;
    bool device_converged() const override;
    std::optional<double> query_param(const std::string& name) const override;
    void reset_temp() override { temp_done_ = false; }

    /// Set initial condition voltages on the underlying UCB instance.
    /// Called by the parser after make() when ic=VDS,VGS is present.
    void set_ic(double vds, bool vds_given, double vgs, bool vgs_given);

    std::vector<NoiseSource> noise_sources(
        double freq, const std::vector<double>& dc_solution) const override;

private:
    explicit HFET2Device(std::string name) : Device(std::move(name)) {}

    mutable hfet2::HFET2Instance inst_{};
    hfet2::HFET2Model* model_ = nullptr;

    std::vector<std::pair<int,int>> journal_;

    double* state0_ = nullptr;
    double* state1_ = nullptr;
    double* state2_ = nullptr;
    int32_t state_base_ = -1;

    mutable bool temp_done_ = false;
    mutable int last_noncon_ = 0;

    int32_t max_neo_node_ = -1;

    std::vector<double> ghost_voltages_;
    std::vector<double> ghost_rhs_;
};

} // namespace neospice
