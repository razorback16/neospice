"""Emit native results for cross-build smoke checks (not an ngspice baseline)."""
import json
import sys
from pathlib import Path
import neospice as ns

root = Path(__file__).resolve().parents[1] / 'circuits'
files = ['mos1_nmos_dc_op.cir', 'mos3_nmos_dc_op.cir', 'bsim3v32_nmos_dc_op.cir',
         'hisim2_nmos_dc_op.cir', 'hisimhv_nmos_dc_op.cir', 'vbic_npn_dc.cir']
sim = ns.Simulator()
cases = []
for filename in files:
    text = (root / filename).read_text()
    circuit = sim.parse(text)
    result = sim.run_dc(circuit)
    assert result.status.converged
    voltages = {}
    for signal in result.signal_names():
        if signal.startswith('v('):
            voltages[signal] = result.voltage(signal[2:-1])
    cases.append({'name': filename, 'netlist': text, 'voltages': voltages})
Path(sys.argv[1]).write_text(json.dumps(cases))
