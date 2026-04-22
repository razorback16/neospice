#pragma once
#include "core/types.hpp"
#include "core/matrix.hpp"
#include "core/pz.hpp"
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
struct MOS1ModelCard;
struct MOS9ModelCard;
struct BSIM3ModelCard;
struct BJTModelCard;
struct JFETModelCard;
struct JFET2ModelCard;
struct DIOModelCard;
struct VBICModelCard;
struct HFET2ModelCard;

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

struct AnalysisCommand {
    enum Type { OP, TRAN, AC, DC_SWEEP, NOISE, TF, SENS, PZ };
    Type type;
    double tran_tstep = 0, tran_tstop = 0;
    bool tran_uic = false;   // Use Initial Conditions
    enum ACMode { DEC, OCT, LIN };
    ACMode ac_mode = DEC;
    int ac_npoints = 10;
    double ac_fstart = 1.0, ac_fstop = 1e6;
    // DC sweep parameters (1 or 2 entries for nested sweep)
    std::vector<DCSweepParam> dc_sweep_params;
    // Noise analysis parameters
    std::string noise_output;      // e.g., "v(out)" — the output voltage node
    std::string noise_input_src;   // e.g., "vin" — the input voltage source name
    // TF analysis parameters
    std::string tf_output;         // e.g., "v(out)" or "i(vout)"
    std::string tf_input_src;      // e.g., "vin" — the input source name
    // SENS analysis parameters
    std::string sens_output;       // e.g., "v(out)" — the output variable
    // PZ analysis parameters
    std::string pz_in_pos, pz_in_neg, pz_out_pos, pz_out_neg;
    PZTransferType pz_transfer = PZTransferType::VOLTAGE;
    PZType pz_type = PZType::BOTH;
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
    void add_jfet2_model_card(std::unique_ptr<JFET2ModelCard> card);
    void add_mos1_model_card(std::unique_ptr<MOS1ModelCard> card);
    void add_mos9_model_card(std::unique_ptr<MOS9ModelCard> card);
    void add_dio_model_card(std::unique_ptr<DIOModelCard> card);
    void add_vbic_model_card(std::unique_ptr<VBICModelCard> card);
    void add_hfet2_model_card(std::unique_ptr<HFET2ModelCard> card);

    /// Take ownership of a BSIM3ModelCard so it outlives any BSIM3Device
    /// that holds a non-owning pointer to it.
    void add_bsim3_model_card(std::unique_ptr<BSIM3ModelCard> card);

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

    std::string node_name(int32_t idx) const;
    int32_t     node_index(const std::string& name) const;

    void mark_internal_node(int32_t idx);
    bool is_internal_node(int32_t idx) const;

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
    std::vector<std::unique_ptr<MOS1ModelCard>> mos1_model_cards_;
    std::vector<std::unique_ptr<MOS9ModelCard>> mos9_model_cards_;
    std::vector<std::unique_ptr<BJTModelCard>> bjt_model_cards_;
    std::vector<std::unique_ptr<JFETModelCard>> jfet_model_cards_;
    std::vector<std::unique_ptr<JFET2ModelCard>> jfet2_model_cards_;
    std::vector<std::unique_ptr<DIOModelCard>> dio_model_cards_;
    std::vector<std::unique_ptr<VBICModelCard>> vbic_model_cards_;
    std::vector<std::unique_ptr<BSIM3ModelCard>> bsim3_model_cards_;
    std::vector<std::unique_ptr<HFET2ModelCard>> hfet2_model_cards_;
    std::unordered_map<std::string, int32_t> node_map_;
    std::vector<std::string>                 node_names_;
    std::vector<bool>                        internal_nodes_;
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
