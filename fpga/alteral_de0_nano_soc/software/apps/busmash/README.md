# busmash — data-bus saturation firmware for SBA arbitration testing

Not a demo. This is a **test** firmware whose only job is to keep the
core's data AHB port as busy as possible, so that the Debug Module's System Bus
Access (SBA) has to fight for it while a debugger reads and writes memory.

Pair it with [`debug/arvern-stress.cfg`](../../../debug/arvern-stress.cfg), which
detects it automatically and polls its control block.

## Why it exists

aRVern's SBA shares the core's data AHB port and **arbitrates** for it, so a
debugger can access memory on a *running* hart (see
[`debug_interface.md`](https://github.com/Arvern-Silicon/arvern/blob/main/doc/debug_interface.md) §1). On the
FPGA that path is only meaningfully tested if the hart is genuinely competing for
the bus (normal firmware like the leds demo does not compete).

## What it does

An unbounded store → load → verify loop over its own private region:

1. write every word with an **address-tagged** pattern (`&word ^ seed`)
2. read every word straight back and compare
3. bump the heartbeat, rotate the seed, repeat

Address-tagged rather than a constant pattern on purpose: a mis-arbitrated
transfer that returns *the wrong word* is caught, not just one that returns
garbage. The loop is `volatile` so it survives `-O2` — confirm with
`grep -cE '\t(sw|lw)' busmash.lst` after any optimization-flag change; if that
drops to zero the compiler ate the loop and the test is measuring nothing.

## The invariant: disjoint memory

The whole cross-talk test rests on the hart and the debugger never touching the
same bytes. `link.ld` enforces that by carving SRAM into three regions:

| Region | Range | Owner |
|---|---|---|
| `SRAM` | `0x80000000` +24K | firmware `.data` / `.bss` / `.stack` |
| `DBG`  | `0x80006000` +4K  | **debugger scratch** — firmware never touches it |
| `HAM`  | `0x80007000` +4K  | hammer region + control block — debugger never *writes* it |

That gives corruption detection in **both** directions:

- **debugger → hart**: the firmware verifies its own region; `ERRS` counts mismatches
- **hart → debugger**: the stress script's address-tagged pattern over `DBG` must
  survive; `STRESS_RAM` in `arvern-stress.cfg` must stay pointed at that window

## Control block

At `__hammer_cb` (`0x80007f00`), polled over SBA while the loop runs:

| Offset | Field | Meaning |
|---|---|---|
| `+0x00` | `MAGIC` | `0x48414D52` (`'HAMR'`) — firmware loaded *and* this layout |
| `+0x04` | `HB` | heartbeat, ++ per pass. **Frozen ⇒ the hart died or stalled** |
| `+0x08` | `ERRS` | readback mismatches. **Nonzero ⇒ debugger corrupted the hart** |
| `+0x0c` | `PASSES` | completed verify passes |
| `+0x10` | `BAD_ADDR` | last mismatch: where |
| `+0x14` | `BAD_GOT` | last mismatch: what was read |
| `+0x18` | `BAD_WANT` | last mismatch: what was written |

`HB` matters as much as `ERRS`: a hammer that silently stopped makes every
subsequent check pass *vacuously*. `MAGIC` only proves it was loaded once.

The offsets are contractual with `arvern-stress.cfg` — change one, change both.
The addresses come from linker symbols rather than literals in the C, and the
magic word makes drift loud (the script reports "hammer firmware NOT detected")
instead of silently passing.

## Build

```bash
make                 # -> busmash.elf (+ .ihex, .lst, .size)
make clean
```

Same toolchain resolution as [`leds`](../leds/README.md) — ISA and ABI come from
the RTL-generated `march_config.sh`, so the build tracks the core you synthesized.
No `mylib`: a library call inside the hammer loop would add unrelated bus traffic
and muddy what is being measured.

## Run

The commands below use the FT232H OpenOCD configs, so the board must carry the JTAG
DTM (`DTM_TYPE=0`, the default build; see [`debug/`](../../../debug/README.md)). Load
it, let it run, then point the stress test at the board:

```bash
cd ../../..                       # -> alteral_de0_nano_soc/
openocd -f debug/arvern-ft232h-gdb.cfg -c halt \
        -c "load_image $PWD/software/apps/busmash/busmash.elf" \
        -c 'reset run' -c shutdown

openocd -c "set STRESS_SOAK 30" \
        -f debug/arvern-ft232h-gdb.cfg -f debug/arvern-stress.cfg
```

The stress run prints `busmash hammer firmware DETECTED …` when it finds the
control block. If it instead says `NOT detected`, the running-hart tests still
execute but are largely **uncontended** — treat those results with suspicion.

Restore the normal demo afterwards:

```bash
openocd -f debug/arvern-ft232h-gdb.cfg -c halt \
        -c "load_image $PWD/software/apps/leds/leds.elf" -c 'reset run' -c shutdown
```

## Interpreting a run

| Symptom | Means |
|---|---|
| `ERRS` nonzero | debugger SBA traffic corrupted hart data — arbitration bug |
| `HB` frozen | hammer stalled/trapped; later results are vacuous, not passing |
| debugger pattern mismatch on `DBG` | hart traffic corrupted debugger data |
| watchpoint fires with no software cause | trigger matching SBA instead of the LSU |
| hart halts by itself | debugger traffic is halting the hart |

## Scope

This proves **correctness** under contention on real hardware. It does not
explore the tight-race timing window: an FT232H at 15 MHz issues SBA requests far
too slowly to contend for many AHB cycles — measured debugger throughput is
unchanged (0.22 s halted vs 0.21 s with the hart saturating the bus). Cycle-level
races belong in simulation, where `debug_sba_running` and the AHB-Lite protocol
checker (`runsim.py -ahb_check`) cover them.
