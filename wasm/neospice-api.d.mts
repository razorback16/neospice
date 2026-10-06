export interface Complex { real: number; imag: number }
export interface Status { converged: boolean; iterations: number; elapsedSeconds: number; warnings: string[] }
export interface DCResult { voltages: Record<string, number>; currents: Record<string, number>; status: Status }
export interface ACResult { frequency: number[]; voltages: Record<string, Complex[]>; currents: Record<string, Complex[]>; status: Status }
export interface TransientResult { time: number[]; voltages: Record<string, number[]>; currents: Record<string, number[]>; status: Status }
export interface GradientResult { outputs: string[]; parameters: string[]; values: number[]; jacobian: number[][]; adjointSolves: number; status: Status }
export interface ACGradientResult { outputs: string[]; parameters: string[]; frequency: number[]; values: Complex[][]; jacobian: Complex[][][]; adjointSolves: number; status: Status }
export interface ReuseStatistics { dcSymbolicAnalyses: number; acSymbolicAnalyses: number; dcRuns: number; acRuns: number }
export interface Circuit {
  updateParam(name: string, value: number): void;
  setTemperature(celsius: number): void;
  dc(): DCResult;
  ac(options?: {mode?: 'dec' | 'oct' | 'lin'; points?: number; start?: number; stop?: number}): ACResult;
  transient(options: {step: number; stop: number}): TransientResult;
  sensitivity(outputs: string[], parameters?: string[]): GradientResult;
  sensitivityAC(outputs: string[], parameters: string[], frequencies: number[]): ACGradientResult;
  reuseStatistics(): ReuseStatistics;
  dispose(): void;
}
export interface ModuleOptions {
  locateFile?: (path: string, prefix: string) => string;
  wasmBinary?: ArrayBuffer | Uint8Array;
  print?: (text: string) => void;
  printErr?: (text: string) => void;
}
export function createNeospice(options?: ModuleOptions): Promise<{
  Circuit: new(netlist: string) => Circuit;
  capabilities: Readonly<Record<'dc' | 'ac' | 'transient' | 'dcAdjoint' | 'linearACAdjoint' | 'incremental' | 'parallel' | 'poleZero', boolean>>;
}>;
