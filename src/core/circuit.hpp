#pragma once
#include "core/types.hpp"
#include "core/matrix.hpp"
#include "core/pz.hpp"
#include "devices/device.hpp"
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace neospice {

struct DCSweepParam {
    std::string source_name;
    double start = 0.0;
    double stop  = 0.0;
    double step  = 0.0;
};

struct StepCommand {
    enum Kind { PARAM, SOURCE, TEMP };
    Kind kind = PARAM;
    std::string name;        // parameter or source name
    double start = 0.0;
    double stop  = 0.0;
    double step  = 0.0;
};

struct MeasureCommand {
    std::string name;           // result name
    std::string analysis_type;  // "tran", "ac", "dc"
    std::string measure_type;   // "trig_targ", "find_when", "avg", "rms", "min", "max", "pp", "integ", "param"

    // For TRIG/TARG:
    std::string trig_signal, targ_signal;
    double trig_val = 0, targ_val = 0;
    std::string trig_direction, targ_direction; // "rise", "fall", "cross"
    int trig_td_count = 1, targ_td_count = 1;  // which crossing (1st, 2nd, etc.)

    // For FIND/WHEN:
    std::string find_signal, when_signal;
    double when_val = 0;
    std::string when_direction; // "rise", "fall", "cross"
    int when_td_count = 1;
    bool at_given = false;      // true if AT= form was used
    double at_val = 0;

    // For statistical (AVG, RMS, MIN, MAX, PP, INTEG):
    std::string signal;
    double from_val = -1e30, to_val = 1e30;  // time/freq range

    // For PARAM:
    std::string param_expr;
};

struct PrintCommand {
    std::string analysis_type;              // "tran", "ac", "dc", "noise"
    std::vector<std::string> signals;       // e.g., {"v(out)", "i(v1)"}
    bool is_plot = false;                   // true for .plot, false for .print
};

struct FourierCommand {
    double fundamental_freq = 0.0;          // Hz — the fundamental frequency
    std::vector<std::string> signals;       // e.g., {"v(out)", "i(r1)"}
};

// Frequency-sweep mode shared by AC and noise analyses.
enum class ACMode { DEC, OCT, LIN };

// Per-analysis command structs — each holds only the fields relevant to that
// analysis type, replacing the old flat AnalysisCommand union-of-everything.
struct OpCmd {};

struct TranCmd {
    double tstep = 0;
    double tstop = 0;
    bool uic = false;   // Use Initial Conditions
};

struct ACCmd {
    ACMode mode = ACMode::DEC;
    int npoints = 10;
    double fstart = 1.0;
    double fstop = 1e6;
};

struct DCSweepCmd {
    std::vector<DCSweepParam> params;  // 1 or 2 entries for nested sweep
};

struct NoiseCmd {
    std::string output;      // e.g., "out" — the output voltage node
    std::string input_src;   // e.g., "vin" — the input voltage source name
    ACMode mode = ACMode::DEC;
    int npoints = 10;
    double fstart = 1.0;
    double fstop = 1e6;
};

struct TFCmd {
    std::string output;      // e.g., "v(out)" or "i(vout)"
    std::string input_src;   // e.g., "vin" — the input source name
};

struct SensCmd {
    std::string output;      // e.g., "v(out)" — the output variable
};

struct PZCmd {
    std::string in_pos, in_neg, out_pos, out_neg;
    PZTransferType transfer = PZTransferType::VOLTAGE;
    PZType type = PZType::BOTH;
};

// The variant-based analysis command type.
using AnalysisCmdVariant = std::variant<OpCmd, TranCmd, ACCmd, DCSweepCmd,
                                        NoiseCmd, TFCmd, SensCmd, PZCmd>;

// AnalysisCommand inherits from the variant so that std::visit works directly,
// while also providing backward-compatible nested type aliases (ACMode, DEC, etc.)
// used by neospice.hpp and test code.
struct AnalysisCommand : AnalysisCmdVariant {
    using AnalysisCmdVariant::AnalysisCmdVariant;
    using AnalysisCmdVariant::operator=;

    // Backward-compat nested type + constants so that existing code such as
    // AnalysisCommand::ACMode, AnalysisCommand::DEC, etc. keeps compiling.
    using ACMode = neospice::ACMode;
    static constexpr auto DEC = ACMode::DEC;
    static constexpr auto OCT = ACMode::OCT;
    static constexpr auto LIN = ACMode::LIN;
};

// Overloaded helper for std::visit with lambdas.
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

// IntegratorCtx is defined in core/types.hpp (included above) so that
// Device (which only includes types.hpp) can reference it for compute_trunc().

struct DeviceInfo {
    std::string name;
    std::string type;
    std::vector<std::string> nodes;
    std::optional<double> value;
};

class Circuit {
public:
    /// Map node name to internal index. "0", "gnd", "GND" → GROUND_INTERNAL (-1)
    int32_t node(const std::string& name);

    int32_t num_nodes() const { return next_node_; }
    int32_t num_vars() const  { return num_vars_; }
    int32_t num_states() const { return num_states_; }

    void add_device(std::unique_ptr<Device> dev);

    /// Take ownership of a model card so it outlives any device that holds
    /// a non-owning pointer to it.  T must be a complete type at the call
    /// site (the template captures the destructor via TypedModelCardHolder).
    template <typename T>
    void add_model_card(std::unique_ptr<T> card) {
        model_cards_.push_back(
            std::make_unique<TypedModelCardHolder<T>>(std::move(card)));
    }

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

    /// Reset all state buffers and device state for a fresh simulation pass.
    void reset_state();

    /// Cache the latest full DC operating-point vector for same-circuit reuse.
    void set_operating_point(const std::vector<double>& solution);
    const std::vector<double>* operating_point() const;
    void clear_operating_point();

    std::string node_name(int32_t idx) const;
    int32_t     node_index(const std::string& name) const;

    void mark_internal_node(int32_t idx);
    bool is_internal_node(int32_t idx) const;
    bool has_organic_diagonal(int32_t idx) const;

    std::vector<std::string> node_names() const;
    std::vector<std::string> device_names() const;
    DeviceInfo device_info(const std::string& name) const;
    std::vector<std::string> devices_at_node(const std::string& node) const;
    Device* find_device(const std::string& name);
    const Device* find_device(const std::string& name) const;
    bool set_param(const std::string& device_name, double value);

    std::string                         title;
    SimOptions                          options;
    std::vector<AnalysisCommand>        analyses;
    std::unordered_map<int32_t, double> ic;       // .ic
    std::unordered_map<int32_t, double> nodeset;  // .nodeset
    std::vector<std::string>            save_signals;
    std::vector<MeasureCommand>         measures;  // .meas / .measure
    std::vector<PrintCommand>           prints;    // .print / .plot
    std::vector<FourierCommand>         fourier_commands;  // .four
    std::vector<StepCommand>            step_commands;     // .step

    IntegratorCtx integrator_ctx;

    // Default construct / destruct / move defined in .cpp so that
    // ModelCardHolder's vtable is emitted in a single TU.
    Circuit();
    ~Circuit();
    Circuit(Circuit&&) noexcept;
    Circuit& operator=(Circuit&&) noexcept;
    Circuit(const Circuit&)            = delete;
    Circuit& operator=(const Circuit&) = delete;

    /// Type-erased wrapper so Circuit can own any model card type without
    /// needing the complete type in this header.  The virtual destructor
    /// ensures proper cleanup through the base pointer.
    struct ModelCardHolder {
        virtual ~ModelCardHolder() = default;
    };

    template <typename T>
    struct TypedModelCardHolder : ModelCardHolder {
        std::unique_ptr<T> card;
        explicit TypedModelCardHolder(std::unique_ptr<T> c) : card(std::move(c)) {}
    };

private:
    void rebind_device_states();  // re-invoke set_state_ptrs on every device

    std::vector<std::unique_ptr<Device>> devices_;
    // Owns model card instances for the lifetime of the Circuit so
    // device non-owning model_ pointers stay valid.
    std::vector<std::unique_ptr<ModelCardHolder>> model_cards_;
    std::unordered_map<std::string, int32_t> node_map_;
    std::vector<std::string>                 node_names_;
    std::vector<bool>                        internal_nodes_;
    std::vector<bool>                        organic_diagonal_;
    int32_t next_node_ = 0;
    int32_t num_vars_  = 0;
    int32_t num_states_ = 0;
    std::vector<double> state0_, state1_, state2_;
    std::vector<double> operating_point_;
    std::unique_ptr<SparsityPattern> pattern_;
};

// Thread-local pointer to the active IntegratorCtx, set by newton_solve
// around each stamp loop so state-storing devices (BSIM4v7) can read
// CKTmode/ag/delta/order during evaluate() without threading another
// parameter through the Device interface.  nullptr outside a Newton pass.
extern thread_local const IntegratorCtx* tls_integrator_ctx;

} // namespace neospice
