# VC Static Lint — chip_example

Signoff-grade structural lint for the chip_example ASIC top: the arvern core,
the AHB interconnect, the peripherals and the ACLINT as one elaboration.
Separate from and additional to the Verilator lint in
`sim/rtl_sim/run/run_lint`; neither replaces the other.

## Usage

Run from this directory, with `vc_static_shell` on PATH:

```bash
./run_vclint                      # default fabric: structural + netlist + quick-lint
./run_vclint -fabric fused        # lint the FUSED interconnect variant
./run_vclint -fabric hiperf       # lint the HIPERF (multi-layer) variant
./run_vclint -lang                # add the LANGUAGE_CHECK stage (slower, noisier)
./run_vclint -top ahb_bus_system  # lint a submodule instead of the whole chip
./run_vclint -raw                 # ignore rules.tcl -- report every enabled rule
./run_vclint -i                   # leave vc_static_shell open for interactive triage
./run_vclint -h                   # full option list
```

Reports land in `results/`, overwritten each run. There is no configuration
sweep: the flow lints the RTL as it stands. `-fabric` is the one build-time
choice the RTL forces, since the interconnect variant is a preprocessor define
and exactly one of the three has to be elaborated.

## What the flow reads

- **Sources** — `rtl/verilog/filelist.f`, the same single-source list synthesis
  uses, flattened by the shared `flatten_filelist.py`. Nothing is listed twice.
- **Fabric variant** — the interconnect has three build-time shapes selected by
  preprocessor define (`-fabric default | fused | hiperf`, the same names
  `run_syn -fabric` uses); one run lints one of them. The define is prepended
  into the flattened filelist.
- **Memory macro** — the real SRAM wrapper is a foundry cell that reaches
  synthesis through `$SRAM_VERILOG_WRAPPER` and is deliberately absent from the
  design filelist. `run_vclint` substitutes `bench/verilog/sram_8kb_wrapper.v`
  so the memory ports stay connected and connectivity checking over the SRAM
  controller interface stays meaningful; findings *inside* the model are waived
  by module scope rather than black-boxed.
- **Configuration** — `chip_example.v` fixes its own parameters, so there is no
  elaboration override and nothing to sweep. `run_vclint` instead harvests the
  parameter values out of that file into `results/rtl_params.tcl` as
  `RTL_PARAM_<NAME>`, which is what `rules.tcl` and `waivers.tcl` branch on.
  This matters most for `ASYNC_RST_EN`: it selects which of the two mutually
  exclusive reset rules is enforced, and getting it wrong flags every flop in
  the chip. Check the run log for `reset style ASYNC` / `reset style SYNC`.

## Status

Clean on all three fabric variants as of 2026-09-13, default stage set
(no `-lang`):

| fabric  | err | warn | stale | waived |
|---------|-----|------|-------|--------|
| default |   0 |    0 |     0 |   2705 |
| fused   |   0 |    0 |     0 |   2816 |
| hiperf  |   0 |    0 |     0 |   2870 |

The first run (2026-09-12) reported `err=20/66/33 warn=7/8/8`. Getting to zero
took one RTL fix and a small set of adjudicated waivers, most of them shared
with the FPGA board; the reasoning for every one is in `waivers.tcl` and in
`rules.tcl`, but the headline dispositions were:

- **RTL fix** — `ahb_interconnect_fused.v` sank its unused AHB sideband nets
  with `&{1'b0, ...}`, which is a constant 0: a structural linter folds it away
  and reports every operand as unloaded (34 findings on the fused variant).
  Changed to `|{1'b0, ...}`, the form this chip's `ahb_bus_system.v` already
  used. Verilator lint unchanged.
- **Rule policy** — `CODING_TREE_NO_FEEDTHROU` (16–32 findings per variant,
  every one AHB fabric wiring) moved into the hierarchy/topology disable block
  of `rules.tcl`, alongside `CODING_HW_SNAKE_PATH`; applied to all four copies.
- **Waivers** — registered feedback through `arv_ipdff` port maps plus the AHB
  hready loop. (The peripheral decoder's unmapped one-hot slots were a waiver
  here at first; they are now sunk explicitly in `ahb_periph_example.v`, where
  the IP's own lint env documents them.)

`stale=0` is the signal to protect: it means every waiver in `waivers.tcl`
matched something. A non-zero count means a waiver has gone stale or is scoped
to a configuration that is not being linted.

It is an invariant of the **full-top, rule-policy, default-stage** run only —
the three rows above. It also holds with `-lang` (re-validated 2026-09-13,
serial `-j 1`: `err=97 warn=37 stale=0`, dominated by naming and
`CODING_ALWAYS_*` house-style tags that `rules.tcl` leaves on). It does not
hold, and is not meant to, under `-raw` (no rule policy, so the violation
population differs), nor under `-top <submodule>`, where waivers scoped to RTL
outside that subtree cannot match by construction.

Benign noise in the log: `RDB_FILTER_FAILED: SIGNAL is not a field of the
specified violation tag ...` for a few tags. That is a signal-name-scoped
waiver being offered to tags that carry no Signal field. Those tags are simply
not waived by it, which is the correct outcome — nothing to fix.

## Policy files

- `rules.tcl` — per-tag enable/disable. Copied **verbatim** from
  `arvern/lint/vc_static/rules.tcl`; it is house policy, not design-specific.
  Keep the copies in step, and make any divergence a deliberate commented one.
- `waivers.tcl` — design waivers. The arvern and ahb_aclint waivers are
  inherited (their RTL is inside this elaboration) and marked as such; the
  SoC-specific block is at the bottom.

Both are commented in place.
