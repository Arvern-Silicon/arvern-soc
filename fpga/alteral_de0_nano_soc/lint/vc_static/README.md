# VC Static Lint — arvern_fpga (DE0-Nano-SoC)

Signoff-grade structural lint for the DE0-Nano-SoC FPGA top: the arvern core,
the AHB interconnect, the board peripherals and the ACLINT as one elaboration.
Separate from and additional to the Verilator lint in
`sim/rtl_sim/run/run_lint`; neither replaces the other.

## Usage

Run from this directory, with `vc_static_shell` on PATH:

```bash
./run_vclint                      # structural + netlist + quick-lint
./run_vclint -lang                # add the LANGUAGE_CHECK stage (slower, noisier)
./run_vclint -top ahb_bus_system  # lint a submodule instead of the whole board
./run_vclint -raw                 # ignore rules.tcl -- report every enabled rule
./run_vclint -i                   # leave vc_static_shell open for interactive triage
./run_vclint -h                   # full option list
```

Reports land in `results/`; the run prints the summary and the file list when it
finishes. There is no fabric axis here: unlike the ASIC's, this board's
`ahb_bus_system.v` has no build-time variants.

## What the flow reads

- **Sources** — `rtl/verilog/filelist.f`, the same single-source list Quartus
  and the simulation use, flattened by the shared `flatten_filelist.py`.
- **Vendor primitives** — Quartus supplies `altsyncram` and
  `cyclonev_io_ibuf/obuf` natively, so the design filelist carries neither.
  `run_vclint` substitutes `bench/verilog/altsyncram.v` and
  `bench/verilog/cyclonev_io.v` — the same models the Verilator lint uses — so
  the memory ports and the bidirectional pads stay connected and connectivity
  checking over them stays meaningful. Findings *inside* the models are waived
  by module scope rather than black-boxed.
- **Configuration** — `arvern_fpga.v` fixes its own parameters, so there is no
  elaboration override. `run_vclint` instead harvests the parameter values out
  of that file into `results/rtl_params.tcl` as `RTL_PARAM_<NAME>`, which is
  what `rules.tcl` and `waivers.tcl` branch on.

  This is load-bearing on this board. It builds **`ASYNC_RST_EN=0`**, and the
  two reset rules are mutually exclusive: `CODING_FF_NO_ASYNC` (every flop must
  be sync-reset) is what belongs here, `CODING_RST_ASYNC_FF` is what belongs on
  the ASIC. Enforcing the wrong one flags every flop on the board. The run log
  must say `reset style SYNC`.

## Status

Clean as of 2026-09-13, default stage set (no `-lang`):

```
err=0  warn=0  info=0  stale=0  waived=4068
```

The first run (2026-09-12) reported `err=82 warn=14`. Getting to zero took one
RTL fix and a set of adjudicated waivers; the reasoning for every one is in
`waivers.tcl` and in `rules.tcl`, but the headline dispositions were:

- **RTL fix** — `ahb_interconnect_fused.v` sank its unused AHB sideband nets
  with `&{1'b0, ...}`, which is a constant 0: a structural linter folds it away
  and reports every operand as unloaded (26 findings). Changed to `|{1'b0, ...}`,
  the form the ASIC's `ahb_bus_system.v` already used. Same fix applied to the
  one such site in this board's `ahb_bus_system.v`. Verilator lint unchanged.
- **Rule policy** — `CODING_TREE_NO_FEEDTHROU` (33 findings, every one AHB
  fabric wiring) moved into the hierarchy/topology disable block of
  `rules.tcl`, alongside `CODING_HW_SNAKE_PATH`; applied to all four copies.
- **Waivers** — `reset_gen` and the cJTAG TCKC domain are async-reset by
  necessity in a sync-reset build; registered feedback through `arv_ipdff`
  port maps; and the bidirectional pad ring (`io_buf`, GPIO_1) which an
  ASIC-oriented ruleset objects to on principle. (The register decoders'
  unmapped one-hot slots were a waiver here at first; they are now sunk
  explicitly in `ahb_periph_example.v` and this board's `ahb_led_key_sw.v`.)

`stale=0` is the signal to protect: it means every waiver in `waivers.tcl`
matched something. A non-zero count means a waiver has gone stale or is scoped
to a configuration that is not being linted.

It is an invariant of the **full-top, rule-policy, default-stage** run only.
The `-lang` stage has been validated on the ASIC side (`stale=0` there) but
**not** on this board; expect to adjudicate the `DO_LANG`-gated waiver block
the first time you pass `-lang` here. It does not hold, and is not meant to,
under `-raw` (no rule policy, so the violation population is different), nor
under `-top <submodule>`, where waivers scoped to RTL outside that subtree
cannot match by construction.

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
  board-specific block is at the bottom. Note that the sync-reset-gated entries
  are the live ones here, the opposite of the ASIC.

Both are commented in place.
