# Priority 2 Device Migration — Design Spec

Date: 2026-04-22

## Goal

Migrate 9 Priority 2 ngspice device models into neospice using the existing
auto-migration tool and 12-phase workflow. Each device gets ngspice comparison
tests. No existing tests may regress.

## Approach

Bottom-up by complexity (Approach A). Three tiers, each completed and merged
before the next begins. Within a tier, devices are migrated by parallel
subagents in isolated git worktrees.

## Tier 1 — Easy (4 parallel subagents)

### HFET2 (2,196 LOC, 13 states)

| Field | Value |
|-------|-------|
| ngspice prefix | `HFET2` |
| neospice name | `hfet2` |
| Instance struct | `sHFET2instance` / `HFET2instance` |
| Model struct | `sHFET2model` / `HFET2model` |
| Terminals | 3: drain (`HFET2drainNode`), gate (`HFET2gateNode`), source (`HFET2sourceNode`) |
| Internal nodes | 2: `HFET2drainPrimeNode`, `HFET2sourcePrimeNode` |
| State count | 13 |
| State base | `HFET2state` |
| Model types | nhfet (1), phfet (-1) via `HFET2type` |
| Geometry | `HFET2length`, `HFET2width`, `HFET2m` |
| Source files | setup: `hfet2setup.c`, load: `hfet2load.c`, temp: `hfet2temp.c`, param: `hfet2param.c`, mpar: `hfet2mpar.c`, devsup: `hfet2.c` |
| Skip files | `hfet2acl.c`, `hfet2ask.c`, `hfet2del.c`, `hfet2dest.c`, `hfet2getic.c`, `hfet2init.c`, `hfet2mask.c`, `hfet2mdel.c`, `hfet2pzl.c`, `hfet2trunc.c` |
| Noise | None (no noise file) |
| AC | Yes (`hfet2acl.c` — manual G/C split) |
| Trunc | Yes (`hfet2trunc.c`) |
| SPICE level | LEVEL=6 on NHFET/PHFET model |
| Parser | New model type `nhfet`/`phfet` → HFET2 device |

### JFET2 (2,453 LOC, 18 states)

| Field | Value |
|-------|-------|
| ngspice prefix | `JFET2` |
| neospice name | `jfet2` |
| Instance struct | `sJFET2instance` / `JFET2instance` |
| Model struct | `sJFET2model` / `JFET2model` |
| Terminals | 3: drain (`JFET2drainNode`), gate (`JFET2gateNode`), source (`JFET2sourceNode`) |
| Internal nodes | 2: `JFET2drainPrimeNode`, `JFET2sourcePrimeNode` |
| State count | 18 |
| State base | `JFET2state` |
| Model types | njf (1), pjf (-1) via `JFET2type` |
| Geometry | `JFET2area`, `JFET2m` |
| Source files | setup: `jfet2set.c`, load: `jfet2load.c`, temp: `jfet2temp.c`, param: `jfet2par.c`, mpar: `jfet2mpar.c`, devsup: `jfet2.c` |
| Skip files | `jfet2acld.c`, `jfet2ask.c`, `jfet2del.c`, `jfet2dest.c`, `jfet2ic.c`, `jfet2init.c`, `jfet2mask.c`, `jfet2mdel.c`, `jfet2noi.c`, `jfet2trun.c` |
| Noise | Yes (`jfet2noi.c` — thermal + flicker) |
| AC | Yes (`jfet2acld.c`) |
| Trunc | Yes (`jfet2trun.c`) |
| Extra | Parker-Skellern model (`psmodel.c` — compile alongside load) |
| SPICE level | LEVEL=2 on NJF/PJF model |
| Parser | `detect_jfet_level()` → LEVEL=2 dispatches to JFET2 |

### MOS3 (8,502 LOC, 17 states)

| Field | Value |
|-------|-------|
| ngspice prefix | `MOS3` |
| neospice name | `mos3` |
| Instance struct | `sMOS3instance` / `MOS3instance` |
| Model struct | `sMOS3model` / `MOS3model` |
| Terminals | 4: drain (`MOS3dNode`), gate (`MOS3gNode`), source (`MOS3sNode`), bulk (`MOS3bNode`) |
| Internal nodes | 2: `MOS3dNodePrime`, `MOS3sNodePrime` |
| State count | 17 |
| State base | `MOS3states` |
| Model types | nmos (1), pmos (-1) via `MOS3type` |
| Geometry | `MOS3w`, `MOS3l`, `MOS3m`, `MOS3drainArea`, `MOS3sourceArea`, `MOS3drainPerimiter`, `MOS3sourcePerimiter`, `MOS3drainSquares`, `MOS3sourceSquares` |
| Source files | setup: `mos3set.c`, load: `mos3load.c`, temp: `mos3temp.c`, param: `mos3par.c`, mpar: `mos3mpar.c`, devsup: `mos3.c` |
| Skip files | `mos3acld.c`, `mos3ask.c`, `mos3conv.c`, `mos3del.c`, `mos3dest.c`, `mos3dist.c`, `mos3dset.c`, `mos3ic.c`, `mos3init.c`, `mos3mask.c`, `mos3mdel.c`, `mos3noi.c`, `mos3pzld.c`, `mos3sacl.c`, `mos3sld.c`, `mos3sprt.c`, `mos3sset.c`, `mos3supd.c`, `mos3trun.c` |
| Noise | Yes (`mos3noi.c` — thermal channel + Rd/Rs + flicker) |
| AC | Yes (`mos3acld.c` — Meyer capacitance G/C split) |
| Trunc | Yes (`mos3trun.c`) |
| SPICE level | LEVEL=3 |
| Parser | `detect_mosfet_level()` → LEVEL=3 dispatches to MOS3 |
| Charge states | 5 Meyer caps (same pattern as MOS1: qgs, qgd, qgb, qbd, qbs) |

### MOS9 (8,539 LOC, 17 states)

| Field | Value |
|-------|-------|
| ngspice prefix | `MOS9` |
| neospice name | `mos9` |
| Instance struct | `sMOS9instance` / `MOS9instance` |
| Model struct | `sMOS9model` / `MOS9model` |
| Terminals | 4: drain (`MOS9dNode`), gate (`MOS9gNode`), source (`MOS9sNode`), bulk (`MOS9bNode`) |
| Internal nodes | 2: `MOS9dNodePrime`, `MOS9sNodePrime` |
| State count | 17 |
| State base | `MOS9states` |
| Model types | nmos (1), pmos (-1) via `MOS9type` |
| Geometry | `MOS9w`, `MOS9l`, `MOS9m`, `MOS9drainArea`, `MOS9sourceArea`, `MOS9drainPerimiter`, `MOS9sourcePerimiter`, `MOS9drainSquares`, `MOS9sourceSquares` |
| Source files | setup: `mos9set.c`, load: `mos9load.c`, temp: `mos9temp.c`, param: `mos9par.c`, mpar: `mos9mpar.c`, devsup: `mos9.c` |
| Skip files | `mos9acld.c`, `mos9ask.c`, `mos9conv.c`, `mos9del.c`, `mos9dest.c`, `mos9dist.c`, `mos9dset.c`, `mos9ic.c`, `mos9init.c`, `mos9mask.c`, `mos9mdel.c`, `mos9noi.c`, `mos9pzld.c`, `mos9sacl.c`, `mos9set.c`, `mos9sld.c`, `mos9sprt.c`, `mos9sset.c`, `mos9supd.c`, `mos9trun.c` |
| Noise | Yes (`mos9noi.c`) |
| AC | Yes (`mos9acld.c` — Meyer capacitance G/C split) |
| Trunc | Yes (`mos9trun.c`) |
| SPICE level | LEVEL=9 |
| Parser | `detect_mosfet_level()` → LEVEL=9 dispatches to MOS9 |
| Charge states | 5 Meyer caps (same as MOS3/MOS1) |
| Note | Nearly identical to MOS3 with enhanced subthreshold model |

## Tier 2 — Medium (2 parallel subagents)

### HFET1 (3,218 LOC, 24 states)

| Field | Value |
|-------|-------|
| ngspice prefix | `HFETA` |
| neospice name | `hfet1` |
| Instance struct | `sHFETAinstance` / `HFETAinstance` |
| Model struct | `sHFETAmodel` / `HFETAmodel` |
| Terminals | 3: drain (`HFETAdrainNode`), gate (`HFETAgateNode`), source (`HFETAsourceNode`) |
| Internal nodes | 5: `HFETAdrainPrimeNode`, `HFETAgatePrimeNode`, `HFETAsourcePrimeNode`, `HFETAdrainPrmPrmNode`, `HFETAsourcePrmPrmNode` |
| State count | 24 |
| State base | `HFETAstate` |
| Model types | nhfet (1), phfet (-1) via `HFETAtype` |
| Geometry | `HFETAlength`, `HFETAwidth`, `HFETAm` |
| Source files | setup: `hfetsetup.c`, load: `hfetload.c`, temp: `hfettemp.c`, param: `hfetparam.c`, mpar: `hfetmpar.c`, devsup: `hfet.c` |
| Skip files | `hfetacl.c`, `hfetask.c`, `hfetdel.c`, `hfetdest.c`, `hfetgetic.c`, `hfetinit.c`, `hfetmask.c`, `hfetmdel.c`, `hfetpzl.c`, `hfettrunc.c` |
| Noise | None |
| AC | Yes (`hfetacl.c`) |
| Trunc | Yes (`hfettrunc.c`) |
| SPICE level | LEVEL=5 on NHFET/PHFET model |
| Parser | Model type dispatch: LEVEL=5 → HFET1 |
| Note | Multi-primed internal nodes (DrmPrm, SrcPrmPrm) for distributed effects |

### BSIM3v32 (13,530 LOC, 17 states)

| Field | Value |
|-------|-------|
| ngspice prefix | `BSIM3v32` |
| neospice name | `bsim3v32` |
| Instance struct | `sBSIM3v32instance` / `BSIM3v32instance` |
| Model struct | `sBSIM3v32model` / `BSIM3v32model` |
| Terminals | 4: drain (`BSIM3v32dNode`), gate (`BSIM3v32gNode`), source (`BSIM3v32sNode`), bulk (`BSIM3v32bNode`) |
| Internal nodes | 3: `BSIM3v32dNodePrime`, `BSIM3v32sNodePrime`, `BSIM3v32qNode` |
| State count | 17 |
| State base | `BSIM3v32states` |
| Model types | nmos (1), pmos (-1) via `BSIM3v32type` |
| Geometry | `BSIM3v32w`, `BSIM3v32l`, `BSIM3v32m`, `BSIM3v32drainArea`, `BSIM3v32sourceArea`, `BSIM3v32drainPerimeter`, `BSIM3v32sourcePerimeter`, `BSIM3v32drainSquares`, `BSIM3v32sourceSquares` |
| Source files | setup: `b3v32set.c`, load: `b3v32ld.c`, temp: `b3v32temp.c`, param: `b3v32par.c`, mpar: `b3v32mpar.c`, devsup: `b3v32.c`, check: `b3v32check.c` |
| Skip files | `b3v32acld.c`, `b3v32noi.c`, `b3v32pzld.c`, `b3v32cvtest.c`, `b3v32ask.c`, `b3v32mask.c`, `b3v32del.c`, `b3v32dest.c`, `b3v32getic.c`, `b3v32trunc.c`, `bsim3v32init.c` |
| Noise | Yes (`b3v32noi.c`) |
| AC | Yes (`b3v32acld.c` — NQS Q-node) |
| Trunc | Yes (`b3v32trunc.c`) |
| SPICE level | LEVEL=8 or 49 (version 3.24) |
| Parser | `detect_mosfet_level()` → LEVEL=8/49 with version check dispatches to BSIM3v32 |
| Cleanup | Size-dependent param linked list (`pSizeDependParamKnot` → `pNext`) |
| Version stamp | `BSIM3v32version` / `BSIM3v32versionGiven` / `"3.24"` |

## Tier 3 — Hard (sequential)

### HiSIM2 (20,549 LOC, 18-21 states)

| Field | Value |
|-------|-------|
| ngspice prefix | `HSM2` |
| neospice name | `hisim2` |
| Instance/Model | `sHSM2instance` / `sHSM2model` |
| Terminals | 4: D,G,S,B (`HSM2dNode`, `HSM2gNode`, `HSM2sNode`, `HSM2bNode`) |
| Internal nodes | 6: D', G', S', B', DB, SB |
| State count | 18 (21 with NQS) |
| State base | `HSM2states` |
| Model types | nmos/pmos via `HSM2type` |
| Geometry | `HSM2_l`, `HSM2_w`, `HSM2_m`, `HSM2_ad`, `HSM2_as` |
| Source files | setup: `hsm2set.c`, load: `hsm2ld.c`, temp: `hsm2temp.c`, param: `hsm2par.c`, mpar: `hsm2mpar.c`, devsup: `hsm2.c` |
| Noise | Yes |
| AC | Yes |
| SPICE level | LEVEL=61/68 |
| Special | Binning parameters, NQS charge model |

### HiSIM_HV (22,762 LOC, 31-36 states)

| Field | Value |
|-------|-------|
| ngspice prefix | `HSMHV` |
| neospice name | `hisimhv` |
| Instance/Model | `sHSMHVinstance` / `sHSMHVmodel` |
| Terminals | 5: D,G,S,B,Sub (`HSMHVdNode`...`HSMHVsubNode`) |
| Internal nodes | 8+: D',G',S',B',DB,SB,tempNode,qiNode,qbNode |
| State count | 31 (36 with NQS) |
| State base | `HSMHVstates` |
| Geometry | `HSMHV_l`, `HSMHV_w`, `HSMHV_m` |
| Source files | setup: `hsmhvset.c`, load: `hsmhvld.c`, temp: `hsmhvtemp.c`, param: `hsmhvpar.c`, mpar: `hsmhvmpar.c`, devsup: `hsmhv.c` |
| SPICE level | LEVEL=62/73 |
| Special | Self-heating (tempNode), 5th terminal (substrate), NQS |

### BSIMSOI (32,103 LOC, 38 states)

| Field | Value |
|-------|-------|
| ngspice prefix | `B4SOI` |
| neospice name | `bsimsoi` |
| Instance/Model | `sB4SOIinstance` / `sB4SOImodel` |
| Terminals | 6: D,G_ext,S,E(substrate),P(body),B (`B4SOIdNode`...`B4SOIbNode`) |
| Internal nodes | 7+: D',S',G,GMid,DB,SB,tempNode + debug nodes |
| State count | 38 |
| State base | `B4SOIstates` |
| Geometry | `B4SOIl`, `B4SOIw`, `B4SOIm`, `B4SOIdrainArea`, `B4SOIsourceArea` |
| Source files | setup: `b4soiset.c`, load: `b4soild.c`, temp: `b4soitemp.c`, param: `b4soipar.c`, mpar: `b4soimpar.c`, devsup: `b4soi.c` |
| SPICE level | LEVEL=10/58 |
| Special | SOI floating body, parasitic BJT, self-heating, GIDL/GISL |

## Per-Device Workflow (12 phases)

Each subagent follows the existing `migrate-device` workflow:

1. **Descriptor** — Create `tools/descriptors/<dev>.yaml`
2. **Auto-migrate** — `python -m ngspice_migrate <dev>.yaml`
3. **Build fix** — Fix includes, macros, matrix stamps until it compiles
4. **AC stamp** — Manual G/C split from the `*acld.c` file
5. **Noise** — Manual implementation from `*noi.c` (if exists)
6. **Truncation** — Wire `compute_trunc()` from charge state offsets
7. **Convergence** — Wire `device_converged()` from convergence checks
8. **IC** — Parse `ic=` on instance line
9. **Query** — Implement `query_param()` for key operating-point values
10. **Parser** — LEVEL dispatch, model parameter table, instance geometry
11. **Tests** — DC OP, DC sweep, AC, transient circuits + ngspice comparison in `tests/devices/<dev>/`
12. **Verify** — Full `ctest` pass (817+ tests, zero regressions)

## Subagent Isolation

- Each device subagent runs in a git worktree (`isolation: "worktree"`)
- Subagents within a tier run in parallel
- After a tier completes, all worktrees are merged to main sequentially
- Build + full test suite verified after each merge

## Success Criteria

- Each device builds and links into neospice
- Each device has at least 3 ngspice comparison tests (DC, AC, transient where applicable)
- Zero regressions in existing 817 tests
- Parser correctly dispatches new LEVEL values
- `docs/device-migration-status.md` updated after each tier

## Risk Mitigation

- Tier 1 validates the auto-migration tool on 4 independent simple devices before scaling
- If a device fails auto-migration, fall back to manual migration using existing P1 devices as templates
- Tier 3 devices are done sequentially so each can benefit from prior experience
- BSIMSOI (largest) is last — maximum accumulated knowledge
