#pragma once
#include "core/types.hpp"
#include "core/matrix.hpp"
#include "devices/device.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace neospice {

// Forward declarations — Circuit owns model card instances on behalf of
// device objects that hold non-owning pointers to their parsed .model.
// Full definitions live in devices/*/device.hpp; we only need
// the complete type in circuit.cpp (for unique_ptr destruction).
struct BSIM4v7ModelCard;
struct BJTModelCard;
struct JFETModelCard;

struct DCSweepParam {
    std::string source_name;
    double start = 0.0;
    double stop  = 0.0;
    double step  = 0.0;
};

struct AnalysisCommand {
    enum Type { OP, TRAN, AC, DC_SWEEP, NOISE };
    Type type;
    double tran_tstep = 0, tran_tstop = 0;
    enum ACMode { DEC, OCT, LIN };
    ACMode ac_mode = DEC;
    int ac_npoints = 10;
    double ac_fstart = 1.0, ac_fstop = 1e6;
    // DC sweep parameters (1 or 2 entries for nested sweep)
    std::vector<DCSweepParam> dc_sweep_params;
    // Noise analysis parameters
    std::string noise_output;      // e.g., "v(out)" — the output voltage node
    std::string noise_input_src;   // e.g., "vin" — the input voltage source name
};

// IntegratorCtx is defined in core/types.hpp (included above) so that
// Device (which only includes types.hpp) can reference it for compute_trunc().

class Circuit {
public:
    /// Map node name to internal index. "0", "gnd", "GND" → GROUND_INTERNAL (-1)
    int32_t node(const std::string& name);

    int32_t num_nodes() const { return next_node_; }
    int32_t num_vars() const  { return num_vars_; }
    int32_t num_states() const { return num_states_; }

    void add_device(std::unique_ptr<Device> dev);

    /// Take ownership of a BSIM4v7ModelCard so it outlives any BSIM4v7Device
    /// that holds a non-owning pointer to it.  The parser calls this once per
    /// distinct .model LEVEL=14 card after building all devices that share it.
    void add_bsim4_model_card(std::unique_ptr<BSIM4v7ModelCard> card);

    /// Take ownership of a BJTModelCard so it outlives any BJTDevice
    /// that holds a non-owning pointer to it.
    void add_bjt_model_card(std::unique_ptr<BJTModelCard> card);

    /// Take ownership of a JFETModelCard so it outlives any JFETDevice
    /// that holds a non-owning pointer to it.
    void add_jfet_model_card(std::unique_ptr<JFETModelCard> card);

    /// Assign branch indices, build sparsity pattern, assign offsets.
    void finalize();

    const SparsityPattern& pattern() const { return *pattern_; }
    const std::vector<std::unique_ptr<Device>>& devices() const { return devices_; }
    std::vector<std::unique_ptr<Device>>& devices()             { return devices_; }

    double* state0() { return state0_.data(); }
    double* state1() { return state1_.data(); }
    double* state2() { return state2_.data(); }

    /// Rotate state history: state2 <- state1 <- state0 (buffer addresses stay stable).
    void rotate_state();

    std::string node_name(int32_t idx) const;
    int32_t     node_index(const std::string& name) const;

    SimOptions                          options;
    std::vector<AnalysisCommand>        analyses;
    std::unordered_map<int32_t, double> ic;       // .ic
    std::unordered_map<int32_t, double> nodeset;  // .nodeset
    std::vector<std::string>            save_signals;

    IntegratorCtx integrator_ctx;

    // Default construct / destruct / move defined in .cpp so BSIM4v7ModelCard
    // can remain an incomplete type at this header's inclusion sites.
    Circuit();
    ~Circuit();
    Circuit(Circuit&&) noexcept;
    Circuit& operator=(Circuit&&) noexcept;
    Circuit(const Circuit&)            = delete;
    Circuit& operator=(const Circuit&) = delete;

private:
    void rebind_device_states();  // re-invoke set_state_ptrs on every device


    std::vector<std::unique_ptr<Device>> devices_;
    // Owns model card instances for the lifetime of the Circuit so
    // device non-owning model_ pointers stay valid.
    std::vector<std::unique_ptr<BSIM4v7ModelCard>> bsim4_model_cards_;
    std::vector<std::unique_ptr<BJTModelCard>> bjt_model_cards_;
    std::vector<std::unique_ptr<JFETModelCard>> jfet_model_cards_;
    std::unordered_map<std::string, int32_t> node_map_;
    std::vector<std::string>                 node_names_;
    int32_t next_node_ = 0;
    int32_t num_vars_  = 0;
    int32_t num_states_ = 0;
    std::vector<double> state0_, state1_, state2_;
    std::unique_ptr<SparsityPattern> pattern_;
};

// Thread-local pointer to the active IntegratorCtx, set by newton_solve
// around each stamp loop so state-storing devices (BSIM4v7) can read
// CKTmode/ag/delta/order during evaluate() without threading another
// parameter through the Device interface.  nullptr outside a Newton pass.
extern thread_local const IntegratorCtx* tls_integrator_ctx;

} // namespace neospice
