import createModule from './neospice.mjs';

function unwrap(envelope) {
  if (!envelope.ok) throw new Error(envelope.error);
  return envelope.value;
}

/** Load an isolated simulator module. Calls are synchronous after this resolves. */
export async function createNeospice(options = {}) {
  const module = await createModule(options);
  class Circuit {
    #native;
    constructor(netlist) {
      this.#native = new module.NativeCircuit();
      try { unwrap(this.#native.load(netlist)); }
      catch (error) { this.dispose(); throw error; }
    }
    #call(method, ...args) {
      if (!this.#native) throw new Error('Circuit has been disposed');
      return unwrap(this.#native[method](...args));
    }
    updateParam(name, value) { this.#call('update', name, value); }
    setTemperature(celsius) { this.#call('temperature', celsius); }
    dc() { return this.#call('dc'); }
    ac({mode = 'dec', points = 20, start = 1, stop = 1e6} = {}) {
      if (!Number.isInteger(points)) throw new TypeError('AC points must be an integer');
      return this.#call('ac', mode, points, start, stop);
    }
    transient({step, stop}) { return this.#call('transient', step, stop); }
    sensitivity(outputs, parameters = []) { return this.#call('sensitivity', outputs, parameters); }
    sensitivityAC(outputs, parameters, frequencies) { return this.#call('sensitivityAC', outputs, parameters, frequencies); }
    reuseStatistics() { return this.#call('statistics'); }
    dispose() {
      this.#native?.delete();
      this.#native = null;
    }
  }
  return {Circuit, capabilities: Object.freeze({dc: true, ac: true, transient: true,
    dcAdjoint: true, linearACAdjoint: true, incremental: true, parallel: false, poleZero: false})};
}
