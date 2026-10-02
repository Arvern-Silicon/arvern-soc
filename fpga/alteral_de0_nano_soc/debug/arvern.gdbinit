# aRVern GDB init for command-line GDB -- server-agnostic (OpenOCD *or*
# arvern-gdbserver on :3333).
#
#   riscv-none-elf-gdb -x debug/arvern.gdbinit software/apps/leds/leds.elf
#
# It opens its own connection to :3333, so do not source it from an IDE (or a
# GDB session) that is already connected.
#
# Both servers as configured in this directory halt the hart on attach and
# advertise the integer registers/pc + the CSRs, so the convenience registers
# ($mstatus, $mcause, $priv, ...) below resolve on either.

set architecture riscv:rv32
set remotetimeout 30

# extended-remote so restart / `monitor reset` behave; server listens on :3333.
target extended-remote :3333

# --- helpers -----------------------------------------------------------------
define reload
  monitor reset halt
  load
end
document reload
Reset the hart (halts at the reset vector) and re-download the ELF.
end

define regs
  info registers
end
document regs
Dump the integer register file (x0..x15 on RV32E, x0..x31 on RV32I) and pc.
end

define trap
  printf "priv    = %d\n",        $priv
  printf "mstatus = 0x%08x\n",    $mstatus
  printf "mcause  = 0x%08x\n",    $mcause
  printf "mepc    = 0x%08x\n",    $mepc
  printf "mtval   = 0x%08x\n",    $mtval
  printf "mtvec   = 0x%08x\n",    $mtvec
end
document trap
Print the machine trap-state CSRs (priv, mstatus, mcause, mepc, mtval, mtvec).
end

# --- bring-up: download, stop at main, ready to run --------------------------
load
tbreak main
echo \n[arvern] loaded; halted before main. 'continue' to run, 'trap' for CSRs.\n
