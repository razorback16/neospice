#pragma once
// Adapter bridging the neospice Device interface to the UCB BJT code.

#include "devices/device.hpp"
#include "devices/bjt/bjt_def.hpp"
#include "devices/bjt/bjt_shim.hpp"
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace neospice {

struct BJTModelCard {
    bjt::BJTModel ucb{};   // aggregate UCB model fields

    BJTModelCard() = default;
    ~BJTModelCard();

    // Non-copyable / non-movable: UCB model is threaded with raw
    // pointers; copying would alias them.
    BJTModelCard(const BJTModelCard&)            = delete;
    BJTModelCard& operator=(const BJTModelCard&) = delete;
};

class BJTDevice : public Device {
public:
    struct Geom {
        double area = 1.0;
        double areab = 1.0;  // defaults to area if not given
        double areac = 1.0;  // defaults to area if not given
        double m = 1.0;
        bool area_given = false;
        bool areab_given = false;
        bool areac_given = false;
        bool m_given = false;
    };

    static std::unique_ptr<BJTDevice> make(
        std::string name, int32_t n_col, int32_t n_base, int32_t n_emit, int32_t n_subst,
        const Geom& geom, BJTModelCard& shared_card);

    // Device interface
    void declare_internal_nodes(Circuit& ckt) override;
    void stamp_pattern(SparsityBuilder& builder) const override;
    void assign_offsets(const SparsityPattern& pattern) override;
    void evaluate(const std::vector<double>& voltages,
                  NumericMatrix& mat, std::vector<double>& rhs) override;
    void ac_stamp(const std::vector<double>& voltages,
                  NumericMatrix& G, NumericMatrix& C) override;

    int32_t state_vars() const override { return 24; }
    void set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) override;
    double compute_trunc(const IntegratorCtx& ctx,
                         const SimOptions& opts) const override;
    bool device_converged() const override;
    std::optional<double> query_param(const std::string& name) const override;
    void reset_temp() override { temp_done_ = false; }

    /// Set initial condition voltages on the underlying UCB instance.
    /// Called by the parser after make() when ic=VBE,VCE is present.
    void set_ic(double vbe, bool vbe_given, double vce, bool vce_given);

private:
    explicit BJTDevice(std::string name) : Device(std::move(name)) {}

    mutable bjt::BJTInstance inst_{};
    bjt::BJTModel* model_ = nullptr;

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
