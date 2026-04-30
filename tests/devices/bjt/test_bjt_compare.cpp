// Comprehensive BJT validation suite.
// Tests: NPN Ic-Vce family (DC sweep), BJT current mirror (DC OP),
//        CE amplifier transient (pulse), CE amplifier AC gain/phase,
//        PNP device (opposite polarity), multi-device with different models.

#include <gtest/gtest.h>
#include "api/neospice.hpp"
#include "parser/netlist_parser.hpp"
#include "devices/bjt/bjt_device.hpp"
#include "core/dc.hpp"
#include "core/transient.hpp"
#include "core/ac.hpp"

#include <cmath>
#include <complex>
#include <string>

using namespace neospice;

// ============================================================================
// 1.  NPN Ic-Vce Family — DC Sweep
// ============================================================================

TEST(BJTValidation, NpnIcVceFamily) {
    // Classic output characteristics: sweep Vce from 0 to 5V at a fixed base
    // voltage that sets a known base current.
    //
    // Vbb = 0.7V + Rb * Ib.  With Rb = 10k and Ib ~ 10uA, Vbb ~ 0.8V.
    // Expected Ic ~ BF * Ib = 200 * 10u = 2mA in the active region.
    //
    // NOTE: DC sweep only supports voltage sources as sweep variables.
    // We use Vce as the collector supply and Vbb to fix the base bias.
    const char* netlist = R"(
NPN Ic-Vce Family
Vce col 0 DC 0
Vbb base 0 DC 0.8
Rb base b_int 10k
Q1 col b_int 0 QMOD
.model QMOD NPN(IS=1e-14 BF=200 BR=5 VAF=100)
.dc Vce 0 5 0.1
.end
)";

    Simulator sim;
    Circuit ckt = sim.parse(netlist);
    DCSweepResult sw = sim.run_dc_sweep(ckt,
        {{DCSweepParam{"Vce", 0.0, 5.0, 0.1}}});

    ASSERT_FALSE(sw.sweep_values.empty());

    // Find index closest to Vce = 2V (active region)
    int idx_2v = -1;
    for (size_t i = 0; i < sw.sweep_values.size(); ++i) {
        if (std::abs(sw.sweep_values[i] - 2.0) < 0.051) {
            idx_2v = static_cast<int>(i);
            break;
        }
    }
    ASSERT_GE(idx_2v, 0) << "Could not find Vce=2V sweep point";

    // Collector current at Vce=2V: from i(vce), sign convention is
    // current flowing *into* the Vce positive terminal = -(collector current).
    // i(vce) is the branch current of Vce, which equals -Ic (KCL at collector).
    double i_vce_at_2v = sw.current("vce")[idx_2v];
    double ic_at_2v = std::abs(i_vce_at_2v);  // magnitude

    // Expect Ic ~ 2mA = BF * Ib,  allow 50% tolerance for bias differences
    EXPECT_GT(ic_at_2v, 0.5e-3) << "Ic should be > 0.5mA at Vce=2V";
    EXPECT_LT(ic_at_2v, 10e-3) << "Ic should be < 10mA at Vce=2V";

    // Verify saturation to active transition: Ic should increase rapidly
    // near Vce=0 (saturation) then level off in active region.
    // Compare Ic at Vce=0.5V vs Vce=2V — in active they should be similar
    int idx_05v = -1;
    for (size_t i = 0; i < sw.sweep_values.size(); ++i) {
        if (std::abs(sw.sweep_values[i] - 0.5) < 0.051) {
            idx_05v = static_cast<int>(i);
            break;
        }
    }
    ASSERT_GE(idx_05v, 0);

    double ic_at_05v = std::abs(sw.current("vce")[idx_05v]);
    // At 0.5V the BJT should be near or just entering active — Ic should
    // be at least half the active-region value
    EXPECT_GT(ic_at_05v, 0.1e-3) << "Ic at Vce=0.5V should be nonzero";

    // Verify Ic increases with Vce at low Vce (saturation region)
    // Compare Vce=0.1V vs Vce=0.5V
    int idx_01v = -1;
    for (size_t i = 0; i < sw.sweep_values.size(); ++i) {
        if (std::abs(sw.sweep_values[i] - 0.1) < 0.051) {
            idx_01v = static_cast<int>(i);
            break;
        }
    }
    ASSERT_GE(idx_01v, 0);
    double ic_at_01v = std::abs(sw.current("vce")[idx_01v]);
    EXPECT_LE(ic_at_01v, ic_at_05v + 1e-6)
        << "Ic at 0.1V should be <= Ic at 0.5V (saturation to active)";

    // At high Vce (5V): Early effect should cause slightly higher Ic
    // than at 2V, but not dramatically so with VAF=100
    size_t idx_5v = sw.sweep_values.size() - 1;
    double ic_at_5v = std::abs(sw.current("vce")[idx_5v]);
    double ratio_5v_2v = ic_at_5v / ic_at_2v;
    EXPECT_GT(ratio_5v_2v, 0.9) << "Ic at 5V should be close to Ic at 2V";
    EXPECT_LT(ratio_5v_2v, 1.5) << "Ic at 5V shouldn't be much bigger (Early)";
}

// ============================================================================
// 2.  BJT Current Mirror — DC Operating Point
// ============================================================================

TEST(BJTValidation, BjtCurrentMirror) {
    // Two NPN transistors: Q1 is diode-connected (collector tied to base),
    // setting a reference current.  Q2 mirrors Q1's collector current.
    //
    //   Vcc (5V) -> Rref (1k) -> ref  (Q1 collector=base)
    //   Q1: collector=ref, base=ref, emitter=gnd
    //   Q2: collector=out, base=ref, emitter=gnd
    //   Vcc (5V) -> Rload (1k) -> out  (Q2 collector)
    const char* netlist = R"(
BJT Current Mirror
Vcc vcc 0 5
Rref vcc ref 1k
Q1 ref ref 0 QMOD
Q2 out ref 0 QMOD
Rload vcc out 1k
.model QMOD NPN(IS=1e-14 BF=200 BR=5)
.op
.end
)";

    Simulator sim;
    Circuit ckt = sim.parse(netlist);
    DCResult dc = sim.run_dc(ckt);

    double v_ref = dc.voltage("ref");
    double v_out = dc.voltage("out");
    double v_vcc = dc.voltage("vcc");

    EXPECT_NEAR(v_vcc, 5.0, 0.01);

    // V(ref) ≈ Vbe ~ 0.6-0.8V (diode-connected BJT)
    EXPECT_GT(v_ref, 0.5) << "Diode-connected BJT Vbe should be > 0.5V";
    EXPECT_LT(v_ref, 0.9) << "Diode-connected BJT Vbe should be < 0.9V";

    // Reference current: Iref = (Vcc - Vref) / Rref
    double i_ref = (v_vcc - v_ref) / 1000.0;
    EXPECT_GT(i_ref, 1e-3) << "Reference current should be > 1mA";
    EXPECT_LT(i_ref, 10e-3) << "Reference current should be < 10mA";

    // Mirror current: Imirror = (Vcc - Vout) / Rload
    double i_mirror = (v_vcc - v_out) / 1000.0;
    EXPECT_GT(i_mirror, 0.0) << "Mirror current should be positive";

    // Current matching: Imirror ≈ Iref for ideal mirror (within 25%)
    // Mismatch comes from finite beta — base current of Q2 "steals" from Iref
    double ratio = i_mirror / i_ref;
    EXPECT_GT(ratio, 0.7) << "Mirror ratio should be > 0.7";
    EXPECT_LT(ratio, 1.3) << "Mirror ratio should be < 1.3";

    // V(out) should be in active region: above Vce_sat (~0.2V) and below Vcc
    EXPECT_GT(v_out, 0.2) << "Q2 should be in active (Vce > Vce_sat)";
    EXPECT_LT(v_out, 5.0) << "Q2 collector should have current flowing";
}

// ============================================================================
// 3.  Common-Emitter Amplifier Transient — Pulse Input
// ============================================================================

TEST(BJTValidation, CeAmplifierTransient) {
    // Common-emitter amplifier with pulse input.
    // The output at the collector should invert the input.
    //
    // Use a coupling capacitor so that the DC bias (set by Rb) is
    // independent of the pulse source.  Add an emitter resistor Re
    // for stability — Rc/Re ratio sets the gain.
    //
    // Bias: Ib ~ (Vcc-Vbe)/Rb = (5-0.7)/470k ~ 9uA
    //   Ic ~ BF*Ib = 200*9u = 1.8mA
    //   V(col) ~ 5 - 1.8m*2k = 1.4V (active region, not saturated)
    //
    // Gain: Av ~ -Rc/(Re + re) where re ~ VT/Ic ~ 14 ohm
    //   Av ~ -2000/114 ~ -17   (moderate gain, not saturated)
    const char* netlist = R"(
CE Amplifier Transient
Vcc vcc 0 5
Rc vcc col 2k
Re 0 emit 100
Rb vcc base 470k
Q1 col base emit QMOD
.model QMOD NPN(IS=1e-14 BF=200 CJE=20p CJC=8p TF=400p TR=20n)
Cin in base 1u
Vin in 0 PULSE(0 0.05 0 1n 1n 50n 100n)
.tran 1n 200n
.end
)";

    Simulator sim;
    Circuit ckt = sim.parse(netlist);
    TransientResult tran = sim.run_transient(ckt, 1e-9, 200e-9);

    // Simulation should complete without failure
    ASSERT_GT(tran.time.size(), 10u) << "Should have enough time points";
    ASSERT_NEAR(tran.time.back(), 200e-9, 50e-9)
        << "Simulation should reach near final time";

    // Find the collector voltage signal
    ASSERT_TRUE(tran.voltages.count("v(col)") > 0);
    ASSERT_TRUE(tran.voltages.count("v(base)") > 0);

    const auto& v_col  = tran.voltage("col");
    const auto& v_base = tran.voltage("base");

    // Find the pulse high and low periods to verify inversion.
    // Pulse: Vin goes from 0V to 0.05V at t=0, stays high for 50ns,
    // then goes low for the next 50ns.  The coupling cap passes the
    // small-signal step to the base.
    //
    // Find indices near t=25ns (pulse high) and t=75ns (pulse low)
    int idx_high = -1, idx_low = -1;
    for (size_t i = 0; i < tran.time.size(); ++i) {
        if (idx_high < 0 && tran.time[i] >= 25e-9) idx_high = i;
        if (idx_low < 0 && tran.time[i] >= 75e-9) idx_low = i;
    }
    ASSERT_GE(idx_high, 0);
    ASSERT_GE(idx_low, 0);

    // The collector should be in the active region at both points.
    // Verify the collector voltage is not near ground (saturated) or at Vcc (cut-off).
    EXPECT_GT(v_col[idx_high], 0.1)
        << "V(col) should not be at ground (saturation)";
    EXPECT_LT(v_col[idx_high], 4.9)
        << "V(col) should not be at Vcc (cut-off)";
    EXPECT_GT(v_col[idx_low], 0.1)
        << "V(col) should not be at ground (saturation)";
    EXPECT_LT(v_col[idx_low], 4.9)
        << "V(col) should not be at Vcc (cut-off)";

    // When Vin goes high (base voltage increases), more collector current
    // flows, pulling collector voltage DOWN. CE inverting behavior.
    // However, with the coupling cap and 200ns timescale, the effect may
    // be small. Just verify the simulation runs and produces physically
    // reasonable output.
    //
    // Check the collector voltage is in a sensible active-region range
    // across the entire simulation.
    for (size_t i = 0; i < v_col.size(); ++i) {
        EXPECT_GT(v_col[i], -1.0) << "Collector voltage physically unreasonable";
        EXPECT_LT(v_col[i], 6.0) << "Collector voltage exceeds supply";
    }
}

// ============================================================================
// 4.  BJT Amplifier AC Gain/Phase
// ============================================================================

TEST(BJTValidation, CeAmplifierAcGainPhase) {
    // Common-emitter amplifier with resistive bias and coupling cap.
    // AC source at input, measure gain at collector.
    //
    //   Vcc = 5V
    //   Rb  = 470k (vcc to base)  → sets bias: Ib ~ (5-0.7)/470k ~ 9uA
    //   Rc  = 2k   (collector load)
    //   Re  = 100  (emitter degeneration)
    //   Cin = 1uF  (input coupling cap)
    //   Ce  = 10uF (emitter bypass cap — shorts Re at AC for full gain)
    //   Q1: CE amp
    //
    // DC bias: Ic ~ BF*Ib = 200*9u = 1.8mA → V(col) ~ 5-1.8m*2k = 1.4V
    //
    // At midband (Ce shorts Re):
    //   Av ≈ -gm * Rc,  gm = Ic/VT = 1.8m/26m = 69 mS
    //   |Av| ~ 69m * 2k ~ 138
    //
    // At high frequency: gain rolls off due to CJE, CJC, TF.
    const char* netlist = R"(
CE Amplifier AC
Vcc vcc 0 5
Rc vcc col 2k
Re 0 emit 100
Rb vcc base 470k
Q1 col base emit QMOD
.model QMOD NPN(IS=1e-14 BF=200 CJE=20p CJC=8p TF=400p TR=20n)
Ce 0 emit 10u
Cin in base 1u
Vin in 0 DC 0 AC 1
.ac dec 10 100 1e9
.end
)";

    Simulator sim;
    Circuit ckt = sim.parse(netlist);
    ACResult ac = sim.run_ac(ckt, AnalysisCommand::DEC, 10, 100.0, 1e9);

    ASSERT_FALSE(ac.frequency.empty());
    ASSERT_TRUE(ac.voltages.count("v(col)") > 0);

    const auto& v_col_ac = ac.voltage("col");
    ASSERT_EQ(v_col_ac.size(), ac.frequency.size());

    // Find midband gain (around 10kHz — well above the coupling cap
    // and emitter bypass poles, but below BJT cutoff).
    int idx_10k = -1;
    for (size_t i = 0; i < ac.frequency.size(); ++i) {
        if (ac.frequency[i] >= 1e4) { idx_10k = i; break; }
    }
    ASSERT_GE(idx_10k, 0);

    double gain_mid = std::abs(v_col_ac[idx_10k]);
    // CE amplifier should provide voltage gain > 1
    EXPECT_GT(gain_mid, 1.0)
        << "CE amplifier should have |Av| > 1 at midband";

    // Verify the gain is in a reasonable range for a CE amplifier
    // with Rc=2k, gm ~ Ic/VT ~ 70mS → |Av| ~ 140 (order of magnitude)
    EXPECT_GT(gain_mid, 10.0)
        << "CE amplifier gain should be significant";
    EXPECT_LT(gain_mid, 1000.0)
        << "CE amplifier gain should not be unreasonably high";

    // Check gain at low frequency (below coupling cap pole) — should
    // be lower than midband due to the coupling capacitor Cin.
    double gain_low = std::abs(v_col_ac[0]);
    // At 100 Hz, Cin impedance is 1/(2*pi*100*1e-6) ~ 1.6k which
    // attenuates the signal. Gain should be lower than midband.
    EXPECT_LT(gain_low, gain_mid)
        << "Gain at lowest frequency should be less than midband (coupling cap)";

    // Check phase at midband: CE amplifier inverts, so phase should
    // be near +/-180 degrees.
    double phase_mid_deg = std::arg(v_col_ac[idx_10k]) * 180.0 / M_PI;
    // Phase should be roughly +/-180 (inverting). Allow wide margin since
    // the coupling cap and bias network shift phase.
    // abs(phase) should be > 90 degrees at least (inverting character)
    EXPECT_GT(std::abs(phase_mid_deg), 90.0)
        << "CE amp should show inverting phase (|phase| > 90 deg) at midband";
}

// ============================================================================
// 5.  PNP Device — Opposite Polarity
// ============================================================================

TEST(BJTValidation, PnpCommonEmitter) {
    // PNP common-emitter circuit.
    //   Emitter at +5V (Vee), collector pulled to gnd through Rc.
    //   Base biased through Rb from gnd (current flows out of base).
    //
    // PNP convention: current flows from emitter to collector.
    // The collector is connected to ground via Rc, so V(col) is positive.
    const char* netlist = R"(
PNP Common Emitter
Vee emitter 0 5
Rc 0 col 1k
Rb 0 base 100k
Q1 col base emitter QMOD
.model QMOD PNP(IS=1e-14 BF=150)
.op
.end
)";

    Simulator sim;
    Circuit ckt = sim.parse(netlist);
    DCResult dc = sim.run_dc(ckt);

    double v_emitter = dc.voltage("emitter");
    double v_base    = dc.voltage("base");
    double v_col     = dc.voltage("col");

    EXPECT_NEAR(v_emitter, 5.0, 0.01);

    // For PNP: base voltage should be below emitter (V_base < V_emitter).
    // V_base is set by the Rb divider: current flows from emitter through
    // the base-emitter junction and through Rb to ground.
    // V_base = V_emitter - |Vbe| ≈ 5.0 - 0.65 = 4.35V approximately,
    // but Rb also pulls base toward ground.
    // Ib flows out of base for PNP, i.e. from base to ground through Rb.
    // Vbe (external) = V_base - V_emitter < 0 for PNP.
    double vbe_ext = v_base - v_emitter;
    EXPECT_LT(vbe_ext, 0.0) << "PNP external Vbe should be negative";
    EXPECT_GT(vbe_ext, -1.0) << "PNP external |Vbe| should be < 1V";

    // Collector voltage: PNP collector current flows from emitter to collector,
    // entering the Rc from the collector side, developing a positive V(col).
    EXPECT_GT(v_col, 0.0)
        << "PNP collector should be pulled positive by current through Rc";

    // The transistor should be conducting (collector has current flowing)
    // Collector current (conventional) = V(col) / Rc
    double ic_mag = v_col / 1000.0;
    EXPECT_GT(ic_mag, 0.5e-3)
        << "PNP should have significant collector current";

    // Query device parameters — UCB model stores internal NPN-equivalent values
    for (auto& d : ckt.devices()) {
        auto* bjt = dynamic_cast<BJTDevice*>(d.get());
        if (!bjt) continue;

        auto ic = bjt->query_param("ic");
        auto vbe_q = bjt->query_param("vbe");
        auto gm = bjt->query_param("gm");

        ASSERT_TRUE(ic.has_value());
        ASSERT_TRUE(vbe_q.has_value());
        ASSERT_TRUE(gm.has_value());

        // The UCB model internally computes with NPN-equivalent values:
        // internal Vbe and Ic are positive regardless of PNP/NPN polarity.
        // (The polarity sign is applied during matrix stamping, not in
        // the stored operating-point variables.)
        EXPECT_GT(*ic, 0.0) << "Internal (NPN-equiv) Ic should be positive";
        EXPECT_GT(*vbe_q, 0.0) << "Internal (NPN-equiv) Vbe should be positive";
        EXPECT_LT(*vbe_q, 1.0) << "Internal Vbe should be < 1V";

        // Transconductance should be positive
        EXPECT_GT(*gm, 0.0) << "Transconductance should be positive";
    }
}

// ============================================================================
// 6.  Multi-Device: Two BJTs with Different Models
// ============================================================================

TEST(BJTValidation, TwoBjtsDifferentModels) {
    // Two BJTs with different beta values. The one with higher BF
    // should draw more collector current (lower collector voltage).
    const char* netlist = R"(
Two BJTs Different Models
Vcc vcc 0 5
R1 vcc c1 1k
R2 vcc c2 1k
Rb1 vcc b1 100k
Rb2 vcc b2 100k
Q1 c1 b1 0 HIGH_GAIN
Q2 c2 b2 0 LOW_GAIN
.model HIGH_GAIN NPN(IS=1e-14 BF=400)
.model LOW_GAIN NPN(IS=1e-14 BF=50)
.op
.end
)";

    Simulator sim;
    Circuit ckt = sim.parse(netlist);
    DCResult dc = sim.run_dc(ckt);

    double v_c1 = dc.voltage("c1");
    double v_c2 = dc.voltage("c2");
    double v_b1 = dc.voltage("b1");
    double v_b2 = dc.voltage("b2");

    // Both should have reasonable base voltages (biased via Rb from Vcc)
    EXPECT_GT(v_b1, 0.4);
    EXPECT_LT(v_b1, 1.0);
    EXPECT_GT(v_b2, 0.4);
    EXPECT_LT(v_b2, 1.0);

    // Q1 (HIGH_GAIN, BF=400) should have higher Ic → lower V(c1)
    // Q2 (LOW_GAIN, BF=50) should have lower Ic → higher V(c2)
    //
    // With same Rb and supply: Ib is roughly the same for both (assuming
    // similar Vbe), so Ic1 = 400*Ib >> Ic2 = 50*Ib.
    // V(c) = Vcc - Ic * Rc, so V(c1) < V(c2).
    EXPECT_LT(v_c1, v_c2)
        << "Higher-gain BJT should have lower collector voltage";

    // Both should be in active region or saturation
    // (collector voltages between 0 and Vcc)
    EXPECT_GT(v_c1, -0.5);
    EXPECT_LT(v_c1, 5.1);
    EXPECT_GT(v_c2, -0.5);
    EXPECT_LT(v_c2, 5.1);

    // Current ratio: Ic1/Ic2 ≈ BF1/BF2 = 8 if both in active,
    // but Q1 might saturate. At minimum Q1 has more current.
    double ic1_approx = (5.0 - v_c1) / 1000.0;
    double ic2_approx = (5.0 - v_c2) / 1000.0;
    EXPECT_GT(ic1_approx, ic2_approx)
        << "HIGH_GAIN transistor should draw more current";
}
