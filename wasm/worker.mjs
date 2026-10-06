import {createNeospice} from './neospice-api.mjs';
const ready = createNeospice();
let circuit, loadedText;
self.onmessage = async ({data}) => {
  const {id, action, netlist, mode, parameter, value} = data;
  try {
    const api = await ready;
    if (!circuit || netlist !== loadedText) {
      const next = new api.Circuit(netlist);
      circuit?.dispose();
      circuit = next;
      loadedText = netlist;
    }
    if (action === 'tune') circuit.updateParam(parameter, value);
    let result;
    if (mode === 'dc') result = circuit.dc();
    else if (mode === 'ac') result = circuit.ac({mode: 'dec', points: 30, start: 1, stop: 1e5});
    else result = circuit.transient({step: 1e-5, stop: 0.01});
    self.postMessage({id, result, statistics: circuit.reuseStatistics()});
  } catch (error) {
    self.postMessage({id, error: String(error.message ?? error)});
  }
};
