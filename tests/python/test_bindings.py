import os

import numpy as np

import neospice

CIRCUITS_DIR = os.path.join(os.path.dirname(__file__), "..", "circuits")


class TestEnums:
    def test_ac_mode_values(self):
        assert hasattr(neospice, "ACMode")
        assert neospice.ACMode.DEC is not None
        assert neospice.ACMode.OCT is not None
        assert neospice.ACMode.LIN is not None

    def test_integration_method_values(self):
        assert hasattr(neospice, "IntegrationMethod")
        assert neospice.IntegrationMethod.TRAPEZOIDAL is not None
        assert neospice.IntegrationMethod.GEAR2 is not None


class TestSimulatorOptions:
    def test_defaults(self):
        opts = neospice.SimulatorOptions()
        assert opts.reltol == 1e-3
        assert opts.abstol == 1e-12
        assert opts.vntol == 1e-6
        assert opts.trtol == 7.0
        assert opts.gmin == 1e-12

    def test_custom_values(self):
        opts = neospice.SimulatorOptions()
        opts.reltol = 1e-4
        assert opts.reltol == 1e-4


class TestSourceSpecs:
    def test_source_spec_defaults(self):
        spec = neospice.SourceSpec()
        assert spec.dc == 0.0
        assert spec.ac_mag == 0.0
        assert spec.ac_phase == 0.0

    def test_pulse_spec_defaults(self):
        spec = neospice.PulseSpec()
        assert spec.v1 == 0.0
        assert spec.v2 == 0.0

    def test_sin_spec_defaults(self):
        spec = neospice.SinSpec()
        assert spec.vo == 0.0
        assert spec.va == 0.0


class TestDCSweepParam:
    def test_fields(self):
        p = neospice.DCSweepParam()
        p.source_name = "V1"
        p.start = 0.0
        p.stop = 5.0
        p.step = 0.1
        assert p.source_name == "V1"
        assert p.stop == 5.0


class TestSimulatorLoadParse:
    def test_load_file(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "resistor_divider.cir"))
        assert isinstance(ckt, neospice.Circuit)
        assert ckt.title == "resistor divider"

    def test_parse_string(self):
        sim = neospice.Simulator()
        ckt = sim.parse("Test\nV1 a 0 DC 1\nR1 a 0 1k\n.op\n.end\n")
        assert isinstance(ckt, neospice.Circuit)
        assert "a" in ckt.node_names()

    def test_circuit_introspection(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "resistor_divider.cir"))
        nodes = ckt.node_names()
        assert "in" in nodes
        assert "mid" in nodes
        devs = ckt.device_names()
        assert len(devs) == 3  # V1, R1, R2


class TestCircuitBuilder:
    def test_build_and_run_dc(self):
        ckt = (neospice.CircuitBuilder()
            .title("Divider")
            .vsource("V1", "in", "0", neospice.SourceSpec())
            .resistor("R1", "in", "out", 1e3)
            .resistor("R2", "out", "0", 1e3)
            .build())
        assert isinstance(ckt, neospice.Circuit)
        assert ckt.title == "divider"


class TestDCResult:
    def test_voltage_divider(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "resistor_divider.cir"))
        result = sim.run_dc(ckt)
        assert isinstance(result, neospice.DCResult)
        assert abs(result.voltage("mid") - 5.0) < 0.01
        assert result.status.converged

    def test_signal_names(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "resistor_divider.cir"))
        result = sim.run_dc(ckt)
        names = result.signal_names()
        assert isinstance(names, list)
        assert len(names) > 0

    def test_diff(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "resistor_divider.cir"))
        result = sim.run_dc(ckt)
        diff = result.diff("in", "mid")
        assert abs(diff - 5.0) < 0.01

    def test_missing_node_raises_key_error(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "resistor_divider.cir"))
        result = sim.run_dc(ckt)
        try:
            result.voltage("nonexistent")
            assert False, "Should have raised"
        except KeyError:
            pass

    def test_current(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "resistor_divider.cir"))
        result = sim.run_dc(ckt)
        i = result.current("v1")
        assert isinstance(i, float)


class TestTransientResult:
    def test_rc_transient(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_lowpass.cir"))
        result = sim.run_transient(ckt, 0.1e-6, 50e-6)
        assert isinstance(result, neospice.TransientResult)
        assert isinstance(result.time, np.ndarray)
        assert result.time.dtype == np.float64
        assert len(result.time) > 10

    def test_voltage_returns_ndarray(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_lowpass.cir"))
        result = sim.run_transient(ckt, 0.1e-6, 50e-6)
        v = result.voltage("out")
        assert isinstance(v, np.ndarray)
        assert v.dtype == np.float64
        assert len(v) == len(result.time)

    def test_signal_names(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_lowpass.cir"))
        result = sim.run_transient(ckt, 0.1e-6, 50e-6)
        names = result.signal_names()
        assert isinstance(names, list)
        assert len(names) > 0

    def test_missing_node_raises_key_error(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_lowpass.cir"))
        result = sim.run_transient(ckt, 0.1e-6, 50e-6)
        try:
            result.voltage("nonexistent")
            assert False, "Should have raised"
        except KeyError:
            pass


class TestACResult:
    def test_rc_ac(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_ac.cir"))
        result = sim.run_ac(ckt, neospice.ACMode.DEC, 10, 100, 1e9)
        assert isinstance(result, neospice.ACResult)
        assert isinstance(result.frequency, np.ndarray)
        assert result.frequency.dtype == np.float64
        assert len(result.frequency) > 5

    def test_complex_voltage(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_ac.cir"))
        result = sim.run_ac(ckt, neospice.ACMode.DEC, 10, 100, 1e9)
        v = result.voltage("out")
        assert isinstance(v, np.ndarray)
        assert v.dtype == np.complex128
        assert len(v) == len(result.frequency)

    def test_magnitude_db(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_ac.cir"))
        result = sim.run_ac(ckt, neospice.ACMode.DEC, 10, 100, 1e9)
        db = result.magnitude_db("out")
        assert isinstance(db, np.ndarray)
        assert db.dtype == np.float64
        assert db[0] > db[-1]  # lowpass: gain drops at high freq

    def test_phase_deg(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_ac.cir"))
        result = sim.run_ac(ckt, neospice.ACMode.DEC, 10, 100, 1e9)
        ph = result.phase_deg("out")
        assert isinstance(ph, np.ndarray)
        assert ph.dtype == np.float64

    def test_missing_node_raises_key_error(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_ac.cir"))
        result = sim.run_ac(ckt, neospice.ACMode.DEC, 10, 100, 1e9)
        try:
            result.voltage("nonexistent")
            assert False, "Should have raised"
        except KeyError:
            pass


class TestNoiseResult:
    def test_rc_noise(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_lowpass_noise.cir"))
        result = sim.run_noise(ckt, "out", "v1", neospice.ACMode.DEC, 10, 100, 10e6)
        assert isinstance(result, neospice.NoiseResult)
        assert isinstance(result.frequency, np.ndarray)
        assert len(result.frequency) > 5
        assert isinstance(result.output_noise_density, np.ndarray)
        assert isinstance(result.input_noise_density, np.ndarray)

    def test_device_names(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_lowpass_noise.cir"))
        result = sim.run_noise(ckt, "out", "v1", neospice.ACMode.DEC, 10, 100, 10e6)
        names = result.device_names()
        assert isinstance(names, list)
        assert len(names) > 0

    def test_integrated_noise(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "rc_lowpass_noise.cir"))
        result = sim.run_noise(ckt, "out", "v1", neospice.ACMode.DEC, 10, 100, 10e6)
        n = result.integrated_output_noise(100, 10e6)
        assert isinstance(n, float)
        assert n > 0


class TestDCSweepResult:
    def test_diode_sweep(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "diode_dc_sweep.cir"))
        p = neospice.DCSweepParam()
        p.source_name = "v1"
        p.start = -1.0
        p.stop = 1.0
        p.step = 0.01
        result = sim.run_dc_sweep(ckt, [p])
        assert isinstance(result, neospice.DCSweepResult)
        assert isinstance(result.sweep_values, np.ndarray)
        assert len(result.sweep_values) > 10
        v = result.voltage("out")
        assert isinstance(v, np.ndarray)
        assert len(v) == len(result.sweep_values)


class TestTFResult:
    def test_resistive_divider_tf(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "tf_resistive_divider.cir"))
        result = sim.run_tf(ckt, "v(out)", "v1")
        assert isinstance(result, neospice.TFResult)
        assert isinstance(result.transfer_function, float)
        assert isinstance(result.input_impedance, float)
        assert isinstance(result.output_impedance, float)
        assert result.status.converged


class TestSensResult:
    def test_divider_sensitivity(self):
        sim = neospice.Simulator()
        ckt = sim.load(os.path.join(CIRCUITS_DIR, "sens_divider.cir"))
        result = sim.run_sens(ckt, "v(out)")
        assert isinstance(result, neospice.SensResult)
        assert isinstance(result.output_value, float)
        assert abs(result.output_value - 5.0) < 0.1
        assert len(result.entries) > 0
        e = result.entries[0]
        assert hasattr(e, "element")
        assert hasattr(e, "sensitivity")
        assert hasattr(e, "normalized")
