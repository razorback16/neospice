"""Filter design case study: adjoint gradients, gradient-based tuning, and
parallel yield analysis with neospice, against ngspice 47 doing the same work.

A. Gradient cost vs. ladder order: neospice adjoint vs. ngspice .sens and
   ngspice in-session finite differences (alter loop).
B. Tuning: inductors rounded to E12 values; capacitors re-tuned by
   Gauss-Newton on the dB response, using each engine's gradients.
C. Yield: 10,000-sample Monte Carlo of the tuned filter with 5% Gaussian
   tolerances on every L and C. ngspice evaluates the same parameter draws.

Usage: NGSPICE=/path/to/ngspice47 python filter_design.py [repetitions]
"""
import cmath, json, math, os, re, statistics, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor
import numpy as np
import neospice as ns
import ladder

HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.path.join(HERE, "work")              # generated netlists
NG = os.environ.get("NGSPICE", "ngspice")   # ngspice 47 executable
ENV = dict(os.environ, OMP_NUM_THREADS="1", OPENBLAS_NUM_THREADS="1")
FREQS = [10 ** (5 + k / 10) for k in range(21)]          # 100 kHz .. 10 MHz
AC_CMD = "ac dec 10 1e5 1e7"
WORKERS = os.cpu_count()

def write(name, text):
    os.makedirs(WORK, exist_ok=True)
    path = os.path.join(WORK, name)
    open(path, "w").write(text)
    return path

def ngspice(path):
    t = time.perf_counter()
    p = subprocess.run([NG, "-b", path], capture_output=True, text=True, env=ENV, cwd=WORK)
    return time.perf_counter() - t, p.stdout

def median_time(fn, reps):
    out, walls = None, []
    for _ in range(reps):
        t = time.perf_counter(); out = fn(); walls.append(time.perf_counter() - t)
    return statistics.median(walls), out

# ---------------------------------------------------------------- engines
def neo_gradient(order, values=None):
    path = write(f"_neo_{order}.cir", ladder.netlist(order, values, analysis=None))
    names = [e[0] for e in ladder.elements(order)[0]]
    r = ns.sensitivity_ac(path, ["v(out)"], names, FREQS)
    assert r.status.converged
    y = np.array([r.values[f][0] for f in range(len(FREQS))])
    jac = np.array(r.jacobian)[:, 0, :]                  # [freq][param]
    return y, jac

def ng_values_fd(order, values=None, rel=1e-6):
    """In-session forward differences: one AC sweep per element plus base."""
    elems = ladder.elements(order)[0]
    vals = {e[0]: (values or {}).get(e[0], e[2]) for e in elems}
    lines = [".control", "set numdgt=16", AC_CMD, "let base = v(out)",
             "print real(base) imag(base)"]
    for name, v in vals.items():
        lines += [f"alter {name} = {v * (1 + rel)!r}", AC_CMD,
                  f"print real(v(out)) imag(v(out))", f"alter {name} = {v!r}", "destroy all"]
    text = ladder.netlist(order, values, analysis=None).replace(".end\n", "") + "\n".join(lines) + "\n.endc\n.end\n"
    wall, out = ngspice(write(f"_ngfd_{order}.cir", text))
    cols = re.findall(r"^\d+\s+\S+\s+(\S+)\s+(\S+)\s*$", out, re.M)
    data = np.array([complex(float(a), float(b)) for a, b in cols])
    n = len(FREQS)
    data = data.reshape(len(vals) + 1, n)
    base = data[0]
    jac = np.stack([(data[k + 1] - base) / (v * rel) for k, v in enumerate(vals.values())], axis=1)
    return wall, base, jac

def ng_baseline(order):
    """ngspice process start, netlist parse and setup with no analysis."""
    text = ladder.netlist(order, None, analysis=None).replace(".end\n", "")
    return ngspice(write(f"_ngbase_{order}.cir", text + ".control\n.endc\n.end\n"))[0]

def ng_sens_c1(stdout):
    rows = re.findall(r"^\d+\s+\S+\s+(\S+)\s+(\S+)\s*$", stdout, re.M)
    return np.array([complex(float(a), float(b)) for a, b in rows])

def ng_sens(order, values=None):
    """ngspice built-in AC sensitivity (all instance/model parameters)."""
    text = ladder.netlist(order, values, analysis=None).replace(".end\n", "")
    text += f".control\nset numdgt=16\nsens v(out) {AC_CMD}\nprint real(c1) imag(c1)\n.endc\n.end\n"
    return ngspice(write(f"_ngsens_{order}.cir", text))

# ---------------------------------------------------------------- part A
def part_a(orders, reps):
    rows = []
    for order in orders:
        nparams = order
        t_neo, (y, jac) = median_time(lambda: neo_gradient(order), reps)
        fd = [ng_values_fd(order) for _ in range(reps)]
        t_fd = statistics.median(f[0] for f in fd)
        _, base, jac_fd = fd[0]
        sens = [ng_sens(order) for _ in range(reps)]
        t_sens = statistics.median(s[0] for s in sens)
        t_base = statistics.median(ng_baseline(order) for _ in range(reps))
        c1 = ng_sens_c1(sens[0][1])
        sens_err = float(np.max(np.abs(c1 - jac[:, 0]) / np.max(np.abs(jac[:, 0]))))
        # Accuracy: adjoint vs ngspice finite differences, and values vs ngspice.
        val_err = float(np.max(np.abs(y - base) / np.maximum(np.abs(base), 1e-12)))
        scale = np.maximum(np.abs(jac_fd), 1e-6 * np.max(np.abs(jac_fd)))
        grad_err = float(np.max(np.abs(jac - jac_fd) / scale))
        row = {"order": order, "parameters": nparams, "neospice_adjoint_s": t_neo,
               "ngspice_fd_alter_s": t_fd, "ngspice_sens_s": t_sens,
               "ngspice_startup_parse_s": t_base,
               "ngspice_sens_c1_max_rel_err_vs_adjoint": sens_err,
               "value_max_rel_err": val_err, "gradient_max_rel_err_vs_fd": grad_err}
        print(json.dumps(row), flush=True)
        rows.append(row)
    return rows

# ---------------------------------------------------------------- part B
E12 = [1.0, 1.2, 1.5, 1.8, 2.2, 2.7, 3.3, 3.9, 4.7, 5.6, 6.8, 8.2]

def e12(v):
    d = 10 ** math.floor(math.log10(v))
    return min((m * d for m in E12 + [10.0]), key=lambda x: abs(math.log(x / v)))

def target_db(order):
    return np.array([-20 * math.log10(2) - 10 * math.log10(1 + (f / ladder.FC) ** (2 * order)) for f in FREQS])

def gauss_newton(order, grad_fn, iters=8):
    elems = ladder.elements(order)[0]
    values = {e[0]: (e12(e[2]) if e[1] == "L" else e[2]) for e in elems}
    names = [e[0] for e in elems]
    # The doubly terminated ladder is symmetric, so |H| has identical
    # sensitivity to mirrored elements and the untied Jacobian is rank
    # deficient. Tune each mirrored capacitor pair as one design variable.
    caps = [k for k, e in enumerate(elems) if e[1] == "C" and k <= order - 1 - k]
    mirror = {k: order - 1 - k for k in caps}
    tgt = target_db(order)
    history = []
    for it in range(iters + 1):
        y, jac = grad_fn(order, values)
        db = 20 * np.log10(np.abs(y))
        resid = db - tgt
        history.append(float(np.sqrt(np.mean(resid ** 2))))
        if it == iters:
            break
        # d(dB)/dp = 20/ln10 * Re(conj(y) dy/dp) / |y|^2 ; log-parameters.
        g = (20 / math.log(10)) * np.real(np.conj(y)[:, None] * jac) / (np.abs(y) ** 2)[:, None]
        p = np.array([values[names[k]] for k in caps])
        cols = [g[:, k] + (g[:, mirror[k]] if mirror[k] != k else 0) for k in caps]
        J = np.stack(cols, axis=1) * p[None, :]
        step, *_ = np.linalg.lstsq(J, -resid, rcond=None)
        for k, s in zip(caps, step):
            scale = math.exp(max(-0.5, min(0.5, s)))
            values[names[k]] *= scale
            if mirror[k] != k:
                values[names[mirror[k]]] *= scale
    return values, history

def part_b(order, reps):
    def neo_fn(o, v): return neo_gradient(o, v)
    def ng_fn(o, v):
        _, base, jac = ng_values_fd(o, v)
        return base, jac
    t_neo, (v_neo, h_neo) = median_time(lambda: gauss_newton(order, neo_fn), reps)
    t_ng, (v_ng, h_ng) = median_time(lambda: gauss_newton(order, ng_fn), reps)
    diff = max(abs(v_neo[k] - v_ng[k]) / v_ng[k] for k in v_neo)
    row = {"order": order, "iterations": len(h_neo) - 1,
           "rms_db_error_history_neospice": h_neo, "rms_db_error_history_ngspice_fd": h_ng,
           "neospice_s": t_neo, "ngspice_fd_s": t_ng, "max_rel_value_difference": diff}
    print(json.dumps(row), flush=True)
    return row, v_neo

# ---------------------------------------------------------------- part C
F_PASS, F_STOP = 0.9, 2.0          # spec frequencies, in units of fc

def spec_pass(mag_pass, mag_stop):
    # Passband: >= -1 dB (re 0.5) at 0.9 fc. Stopband: <= -40 dB at 2 fc.
    return 20 * math.log10(mag_pass / 0.5) >= -1.0 and 20 * math.log10(mag_stop / 0.5) <= -40

def part_c(order, values, n, reps, tol=0.05):
    elems = ladder.elements(order)[0]
    f_lo, f_hi = F_PASS * ladder.FC, F_STOP * ladder.FC
    # ngspice 47 (and neospice) return one point for "lin 2"; lin 3 gives
    # 0.9 fc, 1.45 fc and 2 fc.
    ac = f"ac lin 3 {f_lo!r} {f_hi!r}"
    text = ladder.netlist(order, values, analysis=f".{ac}", params=True)
    path = write(f"_mc_{order}.cir", text)
    variations = []
    for name, kind, v, a, b in elems:
        pv = ns.ParameterVariation()
        pv.parameter = f"p{name}"; pv.nominal = values[name]; pv.spread = tol * values[name]
        pv.distribution = ns.VariationDistribution.Gaussian
        variations.append(pv)
    def neo_run(workers):
        batch = ns.monte_carlo(path, variations, n=n, seed=7, workers=workers)
        passed = []
        for s in batch.samples:
            if s.error:
                passed.append(None); continue
            v = s.result.ac.voltage("out")
            passed.append(spec_pass(abs(v[0]), abs(v[2])))
        return batch, passed
    t1, (_, pass1) = median_time(lambda: neo_run(1), reps)
    tw, (batch, passw) = median_time(lambda: neo_run(WORKERS), reps)
    # ngspice evaluates exactly the parameter sets neospice drew, in sessions
    # that each run an alter loop over a share of the samples.
    draws = [{e[0]: s.point.parameters[f"p{e[0]}"] for e in elems} for s in batch.samples]
    base = ladder.netlist(order, values, analysis=None).replace(".end\n", "")
    def ng_run(sessions):
        paths, index = [], []
        for k in range(sessions):
            lines = [".control", "set numdgt=16"]
            for i in range(k, n, sessions):
                lines += [f"alter {name} = {x!r}" for name, x in draws[i].items()]
                lines += [ac, "print mag(v(out))", "destroy all"]
                index.append(i)
            paths.append(write(f"_ngmc_{k}.cir", base + "\n".join(lines) + "\n.endc\n.end\n"))
        t = time.perf_counter()
        with ThreadPoolExecutor(sessions) as ex:
            outs = list(ex.map(ngspice, paths))
        wall = time.perf_counter() - t
        passed = [None] * n
        mags = []
        for _, out in outs:
            m = [float(x) for x in re.findall(r"^\d+\s+\S+\s+(\S+)\s*$", out, re.M)]
            assert len(m) % 3 == 0, len(m)
            mags += [m[i:i + 3] for i in range(0, len(m), 3)]
        assert len(mags) == n, len(mags)
        for i, m in zip(index, mags):
            passed[i] = spec_pass(m[0], m[2])
        return wall, passed
    ng1 = [ng_run(1) for _ in range(reps)]
    ngw = [ng_run(WORKERS) for _ in range(reps)]
    ng_pass = ngw[0][1]
    agree = sum(a == b for a, b in zip(passw, ng_pass))
    row = {"order": order, "samples": n, "tolerance_sigma": tol,
           "spec": "gain >= -1 dB at 0.9 fc and <= -40 dB at 2 fc",
           "neospice_1_worker_s": t1, f"neospice_{WORKERS}_workers_s": tw,
           "ngspice_1_session_s": statistics.median(r[0] for r in ng1),
           f"ngspice_{WORKERS}_sessions_s": statistics.median(r[0] for r in ngw),
           "failed_samples_neospice": sum(p is None for p in passw),
           "yield_neospice": sum(bool(p) for p in passw) / n,
           "yield_neospice_1_worker": sum(bool(p) for p in pass1) / n,
           "yield_ngspice_same_draws": sum(bool(p) for p in ng_pass) / n,
           "per_sample_pass_fail_agreement": agree / n,
           "serial_vs_parallel_identical": pass1 == passw}
    print(json.dumps(row), flush=True)
    return row

if __name__ == "__main__":
    reps = int(sys.argv[1]) if len(sys.argv) > 1 else 3
    report = {"frequencies": len(FREQS), "workers": WORKERS}
    report["A_gradient_scaling"] = part_a([5, 11, 21, 51, 101, 201], reps)
    report["B_tuning"], tuned = part_b(7, reps)
    report["C_yield"] = part_c(7, tuned, 10000, reps)
    json.dump(report, open(os.path.join(HERE, "results.json"), "w"), indent=1)
