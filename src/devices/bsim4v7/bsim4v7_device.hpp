#pragma once
// Adapter bridging the neospice Device interface to the UCB BSIM4v7 code.

#include "devices/device.hpp"
#include "devices/bsim4v7/bsim4v7_def.hpp"
#include "devices/bsim4v7/bsim4v7_shim.hpp"
#include <memory>
#include <utility>
#include <vector>

namespace neospice {

struct BSIM4v7ModelCard {
    bsim4v7::BSIM4v7Model ucb{};   // aggregate UCB model fields

    BSIM4v7ModelCard() = default;
    ~BSIM4v7ModelCard();
    BSIM4v7ModelCard(BSIM4v7ModelCard&&) noexcept = delete;

    // Non-copyable / non-movable: UCB model is threaded with raw
    // pointers; copying would alias them.
    BSIM4v7ModelCard(const BSIM4v7ModelCard&)            = delete;
    BSIM4v7ModelCard& operator=(const BSIM4v7ModelCard&) = delete;
};

class BSIM4v7Device : public Device {
public:
    struct Geom {
        double W = 1e-6;
        double L = 1e-7;
        double NF = 1.0;
        double AD = 0.0;
        double AS = 0.0;
        double PD = 0.0;
        double PS = 0.0;
        double NRD = 0.0;
        double NRS = 0.0;
        double SA = 0.0;
        double SB = 0.0;
        double SD = 0.0;
    };

    static std::unique_ptr<BSIM4v7Device> make(
        std::string name, int32_t n_d, int32_t n_g, int32_t n_s, int32_t n_b,
        const Geom& geom, BSIM4v7ModelCard& shared_card);

    // Device interface
    void declare_internal_nodes(Circuit& ckt) override;
    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

    int32_t state_vars() const override { return 29; }
    void set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) override;
    double compute_trunc(const IntegratorCtx& ctx,
                         const SimOptions& opts) const override;
    bool device_converged() const override;

    /// Set initial condition voltages on the underlying UCB instance.
    /// Called by the parser after make() when ic=VDS,VGS,VBS is present.
    void set_ic(double vds, bool vds_given,
                double vgs, bool vgs_given,
                double vbs, bool vbs_given);

private:
    explicit BSIM4v7Device(std::string name) : Device(std::move(name)) {}

    mutable bsim4v7::BSIM4v7Instance inst_{};
    bsim4v7::BSIM4v7Model* model_ = nullptr;

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
