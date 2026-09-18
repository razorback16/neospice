# Corpus mismatch triage (checkpoint 36, ngspice 47)

Milestone 3 item 4 asks for every full-corpus mismatch to be triaged by root
cause and model family, classified against the declared supported scope, and
reduced to representative regression circuits. This page is that triage.

Source data: `docs/evidence/joss/2026-09-11-checkpoint36-corpus-run47-records.json.gz`
(67,359 fixture outcomes) and the matching case archive
`2026-09-11-checkpoint36-full-corpus.tar.gz`. Every corpus fixture in that run is
a `.op` deck, so only the `dc_op` column of the
[support matrix](support-matrix.md) is involved.

**This classification is against checkpoint 36 and is not a current-state
report.** Checkpoint 36 predates every phase-3 change. Classification by device
family and root cause carries forward; any claim that a cluster is *fixed*
requires a fresh corpus run and is not made here.

## The scope rule used

The matrix has no row for R, C, L, V, I, E, G, F, H, S, W or the lossless T
line, and every corpus fixture contains at least a resistor and a source. A
literal reading would therefore put the entire corpus outside the claim, which
is useless for triage. This page declares one extension, and it is an extension
rather than a derivation:

- Linear passives (R, C, L, K), independent sources (V, I) and their `.model`
  cards are treated as **verified infrastructure** -- they are exercised inside
  every reference-verified fixture in the matrix, even though no cell is
  attributed to them alone.
- A case is **in scope** when every model-card device in it (D, Q, J, M, Z, O,
  plus B) maps to a matrix row holding a verified `dc_op` cell.
- A case whose only active devices are E, G, F, H, S, W or T -- a behavioral or
  switch-built macromodel with no semiconductor model card -- is **unrowed
  analog**: outside the claim, and counted separately rather than silently
  folded into either side.

The matrix is used to classify corpus outcomes and is never derived from them;
deriving it from corpus agreement would make the classification circular.

## Primary-variant mismatches by class

Each corpus case is run twice: `primary` (the declaration wrapped in a bare
fixture) and `isolated_driven` (the same declaration with a stimulus source).
1,273 primary and 1,272 driven cases mismatch; 743 cases mismatch in both, so
1,802 distinct cases are involved.

| Class | Primary mismatches | Same case, driven variant | Reading |
| --- | --- | --- | --- |
| Unrowed analog | 576 | 264 MISMATCH, 290 NEO_ONLY, 21 MATCH, 1 BOTH_FAIL | Outside the claim |
| Digital (PSpice U devices) | 441 | 415 MISMATCH, 26 NEO_ONLY | Outside the claim, **and silently wrong** |
| In scope | 194 | 181 MATCH, 9 MISMATCH, 4 absent | Defects to fix |
| Unverified model | 62 | 55 MISMATCH, 7 MATCH | Outside the claim |

The decisive column is the third one. **181 of the 194 in-scope primary
mismatches match when the same subcircuit is driven.** The in-scope population
that survives a stimulus is nine cases, not 1,273.

## In scope

194 primary mismatches, in `nec_mos.lib` (119), `GenSemiTVS2.lib` (30),
`GenSemiTVS1.lib` (19), `m_zener.lib` (16), `comlinr.lib` (3), `comlin.lib` (3),
`diode_ST.lib` (2) and `ph_diode.lib` (2). Device kinds: Diode in all 194, MOS3
in 119, BJT in 6. Three root causes, separated by how the disagreement scales
with `gmin`:

### A. A node held only by `gmin` runs away (nec_mos, 119 primary + 3 driven)

`NP80N055ELE` and its siblings are NEC power-MOSFET macromodels whose nonlinear
`Cgd` network contains node 8, connected to nothing but two `DD1` diodes
(`CJO=0`) that are both reverse biased. On the undriven fixture neospice puts
that node at **-100 kV** where ngspice 47 puts it at 4e-16 V; the 100 nA that
`gmin` then leaks through the reverse-biased `DCRR` diode is fed back to the
external terminals by the `FGD` current source, giving 10 mV across the 100 k
test resistors where ngspice gives 1e-22 V.

The signature is `v(x1.8) ∝ 1/gmin²`:

| `gmin` | `v(x1.8)` | `v(net_1)` |
| --- | --- | --- |
| 1e-12 (default) | -1.00019e5 | -9.998e-3 |
| 1e-13 | -1.00138e7 | -9.986e-2 |
| 1e-10 | 1.09e-21 | 2.10e-22 |

The circuit is undriven, so the correct answer is zero everywhere and neospice's
is not merely different from the reference but physically impossible. The
two-diode node reproduces correctly in isolation and with the VCVS control taps
attached, so the runaway needs the surrounding `FGD`/`EVGD` feedback loop.
`abstol`, `reltol`, `itl1` and `gminsteps` do not move it.

### B. A different DC basin in the CLC op-amps (6 cases)

`clc409`, `clc505` and `clc532` (each present in both `comlin.lib` and
`comlinr.lib`) mismatch in **both** variants. These are BJT + diode current-
feedback amplifier macromodels and the disagreement is a genuine operating
point, not a floor artifact: `v(in_m)` 2.478 V vs 6.317 V on `clc409`, 2.186 V
vs 3.241 V on `clc505`, with supply current off by 56 % and 37 %. This is the
only cluster in the corpus that is in scope, survives a stimulus, and is large
in absolute terms.

### C. `gmin` leakage on an undriven net (TVS, zener and diode libraries, ~69)

`GenSemiTVS1/2`, `m_zener`, `diode_ST` and `ph_diode` disagree by microvolts to
tens of microvolts on nets that carry no source. Here the disagreement scales
**linearly** with `gmin` (1e-10 → 1.4 mV, 1e-12 → 3.0 µV, 1e-14 → 30 nV), which
is the signature of the `gmin` floor itself rather than of a model error: the
value of an undriven node is set by where each simulator places its `gmin`
conductances, which is not a defined quantity. 65 of the 69 match once the
subcircuit is driven; the remaining four (`diode_ST` and `ph_diode`, two each)
have no driven variant in the run, so nothing is claimed about them. These are
recorded as ill-posed fixtures. The comparison tolerance is not changed to
accommodate them.

## Digital: outside the claim, and silently wrong

441 primary and 491 driven mismatches are classified here as digital, by the
presence of PSpice digital `.model` types -- `UGATE`, `UEFF`, `UTGATE`, `UGFF`,
`UIO`, `DINPUT`, `DOUTPUT`, `UPLD` -- anywhere in the library the fixture
includes, concentrated in `dig000.lib` (289), `dig874.lib` (44), `analog.lib`
(32), `cmos.lib` (31) and `dig652.lib` (22). That test is by library rather
than by hierarchy, so it over-counts: re-running the 932 fixtures against the
rebuilt parser rejects 898 and leaves 34 (all in `analog.lib`) parsing
normally, because their digital cards sit in sibling subcircuits they never
instantiate. Those 34 are analog voltage references that mismatch for some
other reason and are not triaged here.

ngspice 47 in `ngbehavior=psa` **simulates these**, translating them to its
digital primitives and inserting automatic bridges (the reference output carries
`i(auto_dac3)`). neospice does not implement digital simulation, which is a
legitimate scope boundary. What is not legitimate is how it declines: neospice
**silently drops the card**, emits a floating-node warning, and returns a
successful analog result for a circuit that is missing its devices. On
`74ALS13` that produces `v(out_net) = 0` against the reference's 3.3 V.

This was general, not specific to the primitives: every unrecognized device
letter -- `U`, `N`, `A`, `Y` and any other -- was dropped with a return code of
0, and the digital *interface* devices were worse. An `N` card vanished with no
message at all. An `O` card collides with the LTRA device letter: neospice
resolved its model, saw a `UIO` rather than an `LTRA`, printed
`Warning: O element references non-LTRA model 'IO_STD' -- skipping`, and solved
the rest of the deck, where ngspice 47 calls that a model type mismatch and
stops. This is the "silent unsupported behavior" in this milestone's title, and
the one corpus cluster that violated the project's own standing rule that
unsupported models may remain only if they *fail explicitly*, are documented,
and stay in the corpus accounting.

This has been fixed. `U` cards naming a documented PSpice primitive, `N` cards
carrying the `DGTLNET` interface attribute, and `O` cards whose model is not an
LTRA (the PSpice digital *output* interface collides with the LTRA device
letter, and neospice was skipping the card with a warning where ngspice 47
reports a model type mismatch and stops) are now parse errors.

The rejection keys on the primitive keyword and the interface attribute, not on
the leading letter. A first attempt keyed on the letter plus an instance-name
shape, and its own regression test caught it rejecting the line "Use of this
model is subject to the terms below": vendor libraries carry uncommented prose,
and there are 27,011 non-comment lines in this corpus beginning with `a` alone,
nearly all of them hex data. The keywords accept all 35 primitive types that
occur in the corpus and reject the three `u` lines there that are prose, and the
`DGTLNET` attribute appears on 89 lines, every one of them an interface card.
Four regression tests in `tests/unit/test_parser.cpp` pin the rejection, its
survival through subcircuit expansion, the requirement that prose not trigger
it, and that a real LTRA card still parses.

## Unrowed analog

576 primary mismatches, none carrying a semiconductor model card. The dominant
group is the metal-oxide varistor libraries -- `siov.lib` (281), `zaseries.lib`
(82) and `laseries.lib` (71), 434 together -- where the undriven fixture leaves
neospice at 0 V and ngspice 47 at ±23 V and ±19 V. A prior investigation
adjudicated this cluster as an ngspice self-bias artifact, but that adjudication
was made against ngspice 42 and **has not been re-verified against 47**; it is
recorded here as unadjudicated. The remainder are behavioral op-amp and logic
macromodels (`54ALS.lib`, `54hcxx.lib`, `OpAmp_AD.lib`). All are outside the
claim because E, G, F, H, S, W and T have no matrix row.

## Unverified model

62 primary mismatches that carry a model card neospice has no verified `dc_op`
cell for: JFET level 1 (53 cases) and the PSpice `VSWITCH` switch model (51),
overlapping in 42. JFET level 1 is implemented and exercised but, as the support
matrix records, has no reference-verified operating point -- only JFET2 does.
These are outside the claim as it currently stands; closing them means adding a
reference-verified JFET level 1 `dc_op` cell, not changing a tolerance.

## What this changes

- The in-scope corpus defect list is **two root causes**, A and B above, not a
  thousand-case backlog.
- The largest single cluster in the corpus is a **silent** unsupported-device
  path, which is a defect in how neospice declines rather than in what it
  computes.
- Clusters C, the varistors and the unrowed macromodels are accounting entries,
  not fix targets, and must stay in the corpus accounting as such.
