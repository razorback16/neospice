import os

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
