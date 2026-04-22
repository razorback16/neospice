#pragma once
// Adapter bridging the neospice Device interface to the UCB BSIM3v32 code.

#include "devices/device.hpp"
#include "devices/bsim3v32/bsim3v32_def.hpp"
#include "devices/bsim3v32/bsim3v32_shim.hpp"
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace neospice {

struct BSIM3v32ModelCard {
    bsim3v32::BSIM3v32Model ucb{};   // aggregate UCB model fields

    BSIM3v32ModelCard() = default;
    ~BSIM3v32ModelCard();
    BSIM3v32ModelCard(BSIM3v32ModelCard&&) noexcept = delete;

    // Non-copyable / non-movable: UCB model is threaded with raw
    // pointers; copying would alias them.
    BSIM3v32ModelCard(const BSIM3v32ModelCard&)            = delete;
    BSIM3v32ModelCard& operator=(const BSIM3v32ModelCard&) = delete;
};

class BSIM3v32Device : public Device {
public:
    struct Geom {
        double W = 1e-6;
        double L = 1e-7;
        double AD = 0.0;
        double AS = 0.0;
        double PD = 0.0;
        double PS = 0.0;
        double NRD = 0.0;
        double NRS = 0.0;
        double M  = 1.0;   // device multiplier
    };

    static std::unique_ptr<BSIM3v32Device> make(
        std::string name, int32_t n_d, int32_t n_g, int32_t n_s, int32_t n_b,
        const Geom& geom, BSIM3v32ModelCard& shared_card);

    // Device interface
    void declare_internal_nodes(Circuit& ckt) override;
    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

    int32_t state_vars() const override { return 17; }
    void set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) override;
    double compute_trunc(const IntegratorCtx& ctx,
                         const SimOptions& opts) const override;
    bool device_converged() const override;
    std::optional<double> query_param(const std::string& name) const override;
    void reset_temp() override { temp_done_ = false; }

    std::vector<NoiseSource> noise_sources(
        double freq, const std::vector<double>& dc_solution) const override;

    /// Set initial condition voltages on the underlying UCB instance.
    /// Called by the parser after make() when ic=VDS,VGS,VBS is present.
    void set_ic(double vds, bool vds_given,
                double vgs, bool vgs_given,
                double vbs, bool vbs_given);

private:
    explicit BSIM3v32Device(std::string name) : Device(std::move(name)) {}

    mutable bsim3v32::BSIM3v32Instance inst_{};
    bsim3v32::BSIM3v32Model* model_ = nullptr;

    std::vector<std::pair<int,int>> journal_;

    double* state0_ = nullptr;
    double* state1_ = nullptr;
    double* state2_ = nullptr;
    int32_t state_base_ = -1;

    mutable bool temp_done_ = false;
    mutable int last_noncon_ = 0;
    mutable double sim_temp_ = 300.15;

    int32_t max_neo_node_ = -1;

    std::vector<double> ghost_voltages_;
    std::vector<double> ghost_rhs_;
};

} // namespace neospice
