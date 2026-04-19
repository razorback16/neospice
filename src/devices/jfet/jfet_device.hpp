#pragma once
// Adapter bridging the neospice Device interface to the UCB JFET code.

#include "devices/device.hpp"
#include "devices/jfet/jfet_def.hpp"
#include "devices/jfet/jfet_shim.hpp"
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace neospice {

struct JFETModelCard {
    jfet::JFETModel ucb{};   // aggregate UCB model fields

    JFETModelCard() = default;
    ~JFETModelCard();

    // Non-copyable / non-movable: UCB model is threaded with raw
    // pointers; copying would alias them.
    JFETModelCard(const JFETModelCard&)            = delete;
    JFETModelCard& operator=(const JFETModelCard&) = delete;
};

class JFETDevice : public Device {
public:
    struct Geom {
        double area = 1.0;
        double m = 1.0;
        bool area_given = false;
        bool m_given = false;
    };

    static std::unique_ptr<JFETDevice> make(
        std::string name, int32_t n_drain, int32_t n_gate, int32_t n_source,
        const Geom& geom, JFETModelCard& shared_card);

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

    /// Set initial condition voltages on the underlying UCB instance.
    /// Called by the parser after make() when ic=VDS,VGS is present.
    void set_ic(double vds, bool vds_given, double vgs, bool vgs_given);

private:
    explicit JFETDevice(std::string name) : Device(std::move(name)) {}

    mutable jfet::JFETInstance inst_{};
    jfet::JFETModel* model_ = nullptr;

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
