# IDE remote-debug setup (`:3333`)

Both a running OpenOCD (`arvern-ft232h-gdb.cfg`) and `arvern-gdbserver`
(`run-gdbserver.sh`) expose a GDB stub on `localhost:3333` over the board's default
JTAG build (`DTM_TYPE = 0`; for the other transports see [`README.md`](README.md)). Start
one of those, then point your IDE's *remote GDB* at that port. Use the RISC-V bare-metal GDB
(`riscv-none-elf-gdb`); build the ELF with `-Og`/`-O0` for a clean source view
(`make TC_OPT=-Og` in `../software/apps/leds`).

## VSCode

**Native (`cppdbg`, ms-vscode.cpptools)** — `.vscode/launch.json`, with
`fpga/alteral_de0_nano_soc` opened as the workspace folder:

```jsonc
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "aRVern: GDB on :3333",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/software/apps/leds/leds.elf",
      "cwd": "${workspaceFolder}",
      "MIMode": "gdb",
      "miDebuggerPath": "riscv-none-elf-gdb",
      "miDebuggerServerAddress": "localhost:3333",
      "setupCommands": [
        { "text": "set architecture riscv:rv32" },
        { "text": "-enable-pretty-printing" }
      ],
      "postRemoteConnectCommands": [
        { "text": "monitor reset halt" },
        { "text": "load" }
      ],
      "stopAtConnect": true
    }
  ]
}
```

## CLion

**Run ▸ Edit Configurations… ▸ + ▸ GDB Remote Debug**:

| Field | Value |
|---|---|
| **'target remote' args** | `localhost:3333` |
| **GDB** | `riscv-none-elf-gdb` (or *Bundled* if it's a RISC-V build) |
| **Symbol file** | `…/software/apps/leds/leds.elf` |
| **Download** | *Never* — the firmware is loaded by `monitor`/`load` below |

Then add `monitor reset halt` and `load` to *Advanced GDB Server Options* (or a project
`.gdbinit`) so they run on connect. Do **not** source [`arvern.gdbinit`](arvern.gdbinit)
from CLion: it opens its own connection to `:3333`. It is meant for command-line GDB
(`riscv-none-elf-gdb -x arvern.gdbinit …/leds.elf`); its `reload` helper does
`monitor reset halt` + `load`.

> **The reset command must be `monitor reset halt`, never a bare `monitor reset`.**
> OpenOCD's no-argument `reset` means **`reset run`**, so the hart restarts and *runs*;
> the `Z0` (insert breakpoint) packet the IDE sends immediately afterwards then fails:
>
> ```
> Warn : target arvern.cpu is not halted (add breakpoint)
> Error: can't add breakpoint: target running
> ```
>
> CLion emits this reset itself, from **Settings ▸ Build, Execution, Deployment ▸ Debug
> Profiles ▸ Device Settings ▸ "Reset device commands"** (with *reset after upload*).
> It ships as `monitor reset`, and there is **no common convention** for what a bare
> `reset` means — servers are split down the middle:
>
> | GDB server | bare `monitor reset` |
> |---|---|
> | [`arvern-gdbserver`](https://github.com/Arvern-Silicon/arvern-tools) | **halts** — every variant, deliberately (`gdbserver.py`: *"ALL reset variants leave the hart halted at the reset vector"*) |
> | SEGGER J-Link GDB Server | **halts** — *"Resets and halts the target CPU"*; you resume with `monitor go` |
> | OpenOCD | **runs** — no-argument `reset` is `reset run` |
> | pyOCD | **runs** — `reset` needs `-h`/`--halt` to stop the core |
>
> So for this board **set it to `monitor reset halt`**: unambiguous for OpenOCD and
> accepted by `arvern-gdbserver`, which takes `reset halt` explicitly. Against a J-Link
> the bare `monitor reset` already halts. This setting is IDE-wide, so it is not a value
> to set once and forget across boards.
>
> Keep *reset after upload* enabled: resuming after an upload would run with a PC
> pointing into code that was just overwritten.
>
> On the wire the two are distinguishable in an OpenOCD `-d3` log:
> `qRcmd,7265736574` = `reset`, `qRcmd,72657365742068616c74` = `reset halt`.
>
> Debug Profiles is an IDE-level setting shared across projects. If the reset command is
> ever not editable, this in the cfg *after* `init` — OpenOCD does not register `reset`
> until then — makes the no-argument form halt while leaving explicit `reset run` /
> `reset halt` untouched:
>
> ```tcl
> rename reset ocd_reset_orig
> proc reset {args} {
>     if {[llength $args] == 0} { ocd_reset_orig halt } else { eval ocd_reset_orig $args }
> }
> ```
>
> This is a debugger-side convention mismatch, not a core or OpenOCD defect — prefer
> fixing the IDE's reset command over redefining a standard OpenOCD command.

> Attaching does **not** reset the hart (the `gdb-attach` event only halts it), so GDB
> stops wherever the program happens to be. A breakpoint *behind* that PC will never be
> hit — `break main` on an already-running `leds` looks like a hang, because the demo
> spins inside `main` and never re-enters it. `monitor reset halt` before setting
> breakpoints avoids this; it is the reason the connect commands above exist.

> If CLion/VSCode can't find `riscv-none-elf-gdb` (common on macOS when launched
> from the Dock), give the absolute path to it or start the IDE from a terminal
> that has the toolchain `bin/` on `PATH`.

See [`arvern-tools/doc/tools/gdbserver.md`](https://github.com/Arvern-Silicon/arvern-tools/blob/main/doc/tools/gdbserver.md)
(sibling checkout: [`../../../../arvern-tools/doc/tools/gdbserver.md`](../../../../arvern-tools/doc/tools/gdbserver.md))
for the server side (monitor commands, CSR/`priv` registers, reset semantics).
