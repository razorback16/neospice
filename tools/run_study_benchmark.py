#!/usr/bin/env python3
"""Many-sample study throughput: neospice run_sweep vs ngspice 47.

Each workload varies one resistor over N evenly spaced values. ngspice runs
three ways: one batch session with an `alter` loop (its fastest in-process
path), P such sessions in parallel over disjoint chunks, and one process per
sample with P processes at a time (typical scripted use). neospice runs
Simulator::run_sweep with 1 and P workers, varying either the device value or
the top-level .param. Every sample's probe value is compared with the serial
ngspice session.

Run only on a quiet machine. Usage:
  python tools/run_study_benchmark.py --build build --ngspice /path/to/ngspice \
      --kicad-models third_party/KiCad-Spice-Library/Models --output study-run
"""
import argparse, json, os, platform, re, shutil, statistics, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
ANALYSIS = re.compile(r"^\s*\.(op|ac|tran|end)\b", re.I | re.M)


def netlists(models):
    lt1001 = f'''LT1001 inverting amp from 500 KB vendor lib
.include "{models}/Manufacturer/Linear Technology Corporation/LinearTech.lib"
.param rf=10k
VCC vcc 0 15
VEE vee 0 -15
Vin in 0 0.1
R1 in inm 1k
R2 out inm {{rf}}
X1 0 inm vcc vee out LT1001
RL out 0 10k
.op
.end
'''
    ths = '''THS4131 unity gain
.include ths4131.lib
.param rf=1k
VCC vcc 0 DC 5
VEE vee 0 DC -5
Vin inp 0 DC 0.1 AC 1
Rg1 inp in_p 1k
Rf1 out_p in_p {rf}
Rg2 0 in_n 1k
Rf2 out_n in_n {rf}
Vvocm vocm 0 DC 0
X1 in_p in_n vcc vee out_p out_n vocm THS4131
Rload1 out_p 0 1k
Rload2 out_n 0 1k
.ac dec 10 1 100e6
.end
'''
    rc = '''RC pulse
.param rval=1k
V1 in 0 PULSE(0 1 0 1n 1n 5u 10u)
R1 in out {rval}
C1 out 0 1n
.tran 10n 50u
.end
'''
    return {"lt1001_op.cir": lt1001, "ths_ac.cir": ths, "rc_tran.cir": rc}


WORKLOADS = [
    # id, netlist, devices, param, lo, hi, ngspice analysis, ngspice probe, neospice probe
    ("op_lt1001_vendor_lib", "lt1001_op.cir", ["r2"], "rf", 5e3, 15e3, "op", "v(out)", "out"),
    ("ac_ths4131_81pt", "ths_ac.cir", ["rf1", "rf2"], "rf", 500, 1500, "ac dec 10 1 100e6",
     "vm(out_p)[length(vm(out_p))-1]", "out_p"),
    ("tran_rc_pulse", "rc_tran.cir", ["r1"], "rval", 500, 1500, "tran 10n 50u",
     "v(out)[length(v(out))-1]", "out"),
]


class Runner:
    def __init__(self, args, work):
        self.ng, self.neo, self.work = args.ngspice, args.bench, work
        self.env = dict(os.environ, OMP_NUM_THREADS="1", OPENBLAS_NUM_THREADS="1")

    def run_ng(self, path, probe):
        p = subprocess.run([self.ng, "-b", str(path)], capture_output=True, text=True,
                           env=self.env, cwd=self.work)
        pattern = r"^" + re.escape(probe) + r" = ([-+0-9.eE]+)\s*$"
        return [float(m) for m in re.findall(pattern, p.stdout, re.M)]

    def base_text(self, netlist):
        return ANALYSIS.sub("*", (self.work / netlist).read_text())

    def sessions(self, w, vals, count, tag):
        chunks = [vals[i::count] for i in range(count)]
        paths = []
        for i, chunk in enumerate(chunks):
            lines = [".control", "set numdgt=15"]
            for v in chunk:
                lines += [f"alter {d} = {v!r}" for d in w[2]]
                lines += [w[6], f"print {w[7]}", "destroy all"]
            path = self.work / f"_ng_{tag}_{i}.cir"
            path.write_text(self.base_text(w[1]) + "\n" + "\n".join(lines) + "\n.endc\n.end\n")
            paths.append(path)
        t = time.perf_counter()
        with ThreadPoolExecutor(count) as ex:
            outs = list(ex.map(lambda p: self.run_ng(p, w[7]), paths))
        wall = time.perf_counter() - t
        res = [None] * len(vals)
        for i, out in enumerate(outs):
            res[i::count] = out if len(out) == len(chunks[i]) else [float("nan")] * len(chunks[i])
        for p in paths:
            p.unlink()
        return wall, res

    def per_sample(self, w, vals, par, tag):
        text = (self.work / w[1]).read_text()
        paths = []
        for i, v in enumerate(vals):
            t = re.sub(rf"^\.param\s+{w[3]}\s*=.*$", f".param {w[3]}={v!r}", text, flags=re.M | re.I)
            t = re.sub(r"^\s*\.end\s*$", "", t, flags=re.M | re.I)
            t += f"\n.control\nset numdgt=15\nrun\nprint {w[7]}\n.endc\n.end\n"
            path = self.work / f"_ps_{tag}_{i}.cir"
            path.write_text(t)
            paths.append(path)
        t0 = time.perf_counter()
        with ThreadPoolExecutor(par) as ex:
            outs = list(ex.map(lambda p: self.run_ng(p, w[7]), paths))
        wall = time.perf_counter() - t0
        for p in paths:
            p.unlink()
        return wall, [o[0] if len(o) == 1 else float("nan") for o in outs]

    def neospice(self, w, n, mode, workers):
        names = ",".join(w[2]) if mode == "device" else w[3]
        out = self.work / "_neo_values.txt"
        p = subprocess.run([self.neo, w[1], mode, names, repr(w[4]), repr(w[5]), str(n),
                            str(workers), w[8], str(out)], capture_output=True, text=True,
                           env=self.env, cwd=self.work, check=True)
        r = json.loads(p.stdout)
        vals = [float(x) for x in out.read_text().split()]
        out.unlink()
        return r["wall_ms"] / 1e3, vals


def max_rel(a, b):
    worst = 0.0
    for x, y in zip(a, b):
        if x != x or y != y:
            return float("inf")
        worst = max(worst, abs(x - y) / max(abs(y), 1e-9))
    return worst


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build", required=True, type=Path)
    ap.add_argument("--ngspice", required=True)
    ap.add_argument("--kicad-models", required=True, type=Path)
    ap.add_argument("--output", required=True, type=Path)
    ap.add_argument("--samples", type=int, default=1000)
    ap.add_argument("--reps", type=int, default=5)
    ap.add_argument("--parallel", type=int, default=os.cpu_count())
    args = ap.parse_args()
    args.bench = str((args.build / "tests" / "bench_study").resolve())
    # ngspice runs with cwd set to the work directory, so pin the binary now.
    ng = shutil.which(args.ngspice)
    if ng is None:
        sys.exit(f"ngspice not found: {args.ngspice}")
    args.ngspice = str(Path(ng).resolve())
    if args.output.exists():
        sys.exit(f"{args.output} exists; choose a new output directory")
    work = args.output.resolve()
    work.mkdir(parents=True)
    for name, text in netlists(args.kicad_models.resolve()).items():
        (work / name).write_text(text)
    shutil.copy(REPO / "tests" / "circuits" / "ths4131.lib", work / "ths4131.lib")

    r = Runner(args, work)
    n, reps, par = args.samples, args.reps, args.parallel
    commit = subprocess.run(["git", "-C", str(REPO), "rev-parse", "HEAD"],
                            capture_output=True, text=True).stdout.strip()
    version = subprocess.run([args.ngspice, "--version"], capture_output=True, text=True).stdout
    report = {"samples": n, "repetitions": reps, "parallelism": par, "commit": commit,
              "ngspice_version": version.strip().splitlines()[:2], "cpu_count": os.cpu_count(),
              "platform": platform.platform(), "load_before": os.getloadavg(), "workloads": []}
    for w in WORKLOADS:
        vals = [w[4] + (w[5] - w[4]) * i / max(1, n - 1) for i in range(n)]
        rows, ref = {}, None
        def record(key, fn):
            nonlocal ref
            walls, res = [], None
            for _ in range(reps):
                wall, res = fn()
                walls.append(wall)
            if ref is None:
                ref = res
            rows[key] = {"median_s": statistics.median(walls), "walls_s": walls,
                         "samples_per_s": n / statistics.median(walls),
                         "failed": sum(1 for x in res if x != x),
                         "max_rel_vs_ngspice_serial": max_rel(res, ref)}
            print(w[0], key, json.dumps(rows[key]), flush=True)
        record("ngspice_1_session", lambda: r.sessions(w, vals, 1, "a1"))
        record(f"ngspice_{par}_sessions", lambda: r.sessions(w, vals, par, "ap"))
        record(f"ngspice_process_per_sample_x{par}", lambda: r.per_sample(w, vals, par, "b"))
        for mode in ("device", "param"):
            record(f"neospice_{mode}_1_worker", lambda: r.neospice(w, n, mode, 1))
            record(f"neospice_{mode}_{par}_workers", lambda: r.neospice(w, n, mode, par))
        report["workloads"].append({"id": w[0], "results": rows})
    report["load_after"] = os.getloadavg()
    (work / "study.json").write_text(json.dumps(report, indent=1))


if __name__ == "__main__":
    main()
