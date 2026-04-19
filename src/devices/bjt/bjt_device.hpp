#pragma once
// Adapter bridging the neospice Device interface to the UCB BJT code.

#include "devices/device.hpp"
#include "devices/bjt/bjt_def.hpp"
#include "devices/bjt/bjt_shim.hpp"
#include <memory>
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
        double areab = 1.0;
        double areac = 1.0;
        double m = 1.0;
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

    int32_t state_vars() const override { return 24; }
    void set_state_ptrs(double* s0, double* s1, double* s2, int32_t base) override;

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

    int32_t max_neo_node_ = -1;

    std::vector<double> ghost_voltages_;
    std::vector<double> ghost_rhs_;
};

} // namespace neospice
