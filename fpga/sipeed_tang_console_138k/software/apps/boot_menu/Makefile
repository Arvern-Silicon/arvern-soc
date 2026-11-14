
NAME    = boot_menu

# Load MARCH and toolchain configuration from auto-generated file (based on RTL config)
MARCH_CONFIG_FILE = ../../../sim/rtl_sim/run/march_config.sh
ifneq ($(wildcard $(MARCH_CONFIG_FILE)),)
    MARCH_STD  := $(shell bash -c 'source $(MARCH_CONFIG_FILE) && echo $$MARCH_STD')
    MARCH_COMP := $(shell bash -c 'source $(MARCH_CONFIG_FILE) && echo $$MARCH_COMP')
    MABI       := $(shell bash -c 'source $(MARCH_CONFIG_FILE) && echo $${MABI:-ilp32}')
    CC         := $(shell bash -c 'source $(MARCH_CONFIG_FILE) && echo $$TC_CC')
    OBJCOPY    := $(shell bash -c 'source $(MARCH_CONFIG_FILE) && echo $$TC_OBJCOPY')
    OBJDUMP    := $(shell bash -c 'source $(MARCH_CONFIG_FILE) && echo $$TC_OBJDUMP')
    OBJSIZE    := $(shell bash -c 'source $(MARCH_CONFIG_FILE) && echo $$TC_SIZE')
else
    $(warning march_config.sh not found, using default values)
    MARCH_STD  = rv32i
    MARCH_COMP = rv32ic
    MABI       = ilp32
    CC         = riscv64-unknown-elf-gcc
    OBJCOPY    = riscv64-unknown-elf-objcopy
    OBJDUMP    = riscv64-unknown-elf-objdump
    OBJSIZE    = riscv64-unknown-elf-size
endif

# Optimization level for test firmware. Overridable so a source-level debug
# build can drop it, e.g.  make debug TC_OPT=-Og  (or -O0 for the cleanest
# 1:1 source<->instruction mapping in the debugger's C & Assembler view).
TC_OPT    ?= -O2

# Set architecture based on instruction mode (from environment variable); the
# Tang core has the compressed extensions, so they are the default here
INST_MODE ?= COMP_MODE
ifeq ($(INST_MODE),COMP_MODE)
    MARCH = $(MARCH_COMP)
else
    MARCH = $(MARCH_STD)
endif

# -g emits DWARF debug info for the debugger's C & Assembler (source) view. It
# costs nothing on the target: debug info lands in non-allocated .debug_* ELF
# sections that are never part of a PT_LOAD segment, so the loaded firmware is
# byte-identical -- only the host-side .elf grows.
# -ffunction-sections / --gc-sections drop the driver functions the app does
# not call.
CFLAGS  = -march=$(MARCH) -mabi=$(MABI) -nostartfiles -Wall -Wextra -g $(TC_OPT) \
          -ffunction-sections -fdata-sections -Wl,--gc-sections -DSCR_W=480u -DSCR_H=272u \
          $(EXTRA_CFLAGS) $(FLAVOR_CFLAGS)

all: ${NAME}.elf ${NAME}.ihex ${NAME}.lst ${NAME}.size ${NAME}_sim.elf ${NAME}_sim.ihex ${NAME}_sim.lst \
     sim/sd_image.mem

# The simulated microSD card: a small FAT image with two test programs, one
# small (read straight into place), one of APPB_PAD bytes and more: by
# default it reaches past the loader block, so its load has all three parts
# (in place, staged, in place); 98304 keeps it below the block
APPB_PAD ?= 162816
sim/app_a.bin: sim/apptest.S sim/apptest.ld Makefile
	${CC} -march=$(MARCH) -mabi=$(MABI) -nostdlib -DAPP_ID=0xA1 -DAPP_PAD=64 -T sim/apptest.ld $< -o sim/app_a.elf
	${OBJCOPY} -O binary sim/app_a.elf $@
sim/app_b.bin: sim/apptest.S sim/apptest.ld Makefile
	${CC} -march=$(MARCH) -mabi=$(MABI) -nostdlib -DAPP_ID=0xB2 -DAPP_PAD=$(APPB_PAD) -T sim/apptest.ld $< -o sim/app_b.elf
	${OBJCOPY} -O binary sim/app_b.elf $@
# APPB_CRC=1 also gives APPB an APP.CRC: the loader then checks the image the
# test starts (APPA always has one)
APPB_CRC ?=
sim/sd_image.mem: sim/gen_card.py sim/app_a.bin sim/app_b.bin Makefile
	python3 sim/gen_card.py sim $(if $(APPB_CRC),crc)

# Simulation flavour, built alongside the hardware one: the same sources with
# -DSIMULATION and the simulation script's SIM_FW_DEFINES (${NAME}_sim.*); the
# hardware build (${NAME}.*) never carries them. SIM_STAMP records the defines
# of the last build: a change of SIM_FW_DEFINES deletes the simulation objects
# and ELF, so they are rebuilt (a stamp timestamp alone can tie with the ELF's
# at 1 s resolution).
SIM_FW_DEFINES ?=
SIM_STAMP      := .sim_defines
$(shell printf '%s\n' '$(SIM_FW_DEFINES)' | cmp -s - $(SIM_STAMP) || { printf '%s\n' '$(SIM_FW_DEFINES)' > $(SIM_STAMP); rm -rf obj/sim ${NAME}_sim.elf; })

# Drivers: SPI (+ its SD-card layer), USB host (+ its HID decoder), I2S,
# GPIO, PLIC, Belenos, ACLINT (arvern-ips, arvern-belenos), board support
# (../../bsp), FatFs (../../lib/fatfs), the shared display layer
# (../../lib/demo_kit)
ROOT       = $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../../..)
SPI_SW     = $(ROOT)/arvern-ips/apb_spi/sw
USB_SW     = $(ROOT)/arvern-ips/apb_usb_host/sw
I2S_SW     = $(ROOT)/arvern-ips/apb_i2s/sw
GPIO_SW    = $(ROOT)/arvern-ips/apb_gpio/sw
PLIC_SW    = $(ROOT)/arvern-ips/ahb_plic/sw
ACLINT_SW  = $(ROOT)/arvern-ips/ahb_aclint/sw
BLNS_SW    = $(ROOT)/arvern-belenos/sw
BLNS_PLATFORM = -I$(abspath ../../bsp) -DBLNS_PLATFORM_HEADER='"tang_blns.h"'   # the board's Belenos features (belenos_hw.h)
BSP_DIR    = ../../bsp
FATFS_DIR  = ../../lib/fatfs
KIT_DIR    = ../../lib/demo_kit
DRV_SRC    = $(SPI_SW)/src/apb_spi.c $(SPI_SW)/sd/apb_spi_sd.c $(ACLINT_SW)/src/ahb_aclint.c \
             $(BLNS_SW)/src/belenos.c $(BLNS_SW)/src/blns_gpu.c $(BLNS_SW)/src/blns_dpi.c \
             $(BLNS_SW)/src/blns_ddc.c $(BLNS_SW)/src/blns_hotplug.c \
             $(USB_SW)/src/apb_usb_host.c $(USB_SW)/hid/apb_usb_host_hid.c \
             $(I2S_SW)/src/apb_i2s.c $(GPIO_SW)/src/apb_gpio.c $(PLIC_SW)/src/ahb_plic.c \
             $(BSP_DIR)/led_key_sw.c $(FATFS_DIR)/ff.c $(FATFS_DIR)/sd_diskio.c $(KIT_DIR)/demo_kit.c $(BSP_DIR)/tang_video.c
DRV_INC    = -I$(SPI_SW)/include -I$(SPI_SW)/sd -I$(USB_SW)/include -I$(USB_SW)/hid \
             -I$(I2S_SW)/include -I$(GPIO_SW)/include -I$(PLIC_SW)/include \
             -I$(ACLINT_SW)/include -I$(BLNS_SW)/include $(BLNS_PLATFORM) \
             -I$(BSP_DIR) -I$(FATFS_DIR) -I$(KIT_DIR)
APP_SRC    = main.c gfx.c intro.c audio.c synth.c loader.c sdlink.c

# Every header the build can include (local, board support, drivers, shared
# libraries): a change to one rebuilds the firmware
HDRS = $(wildcard *.h */*.h $(addsuffix /*.h,$(patsubst -I%,%,$(filter -I%,$(DRV_INC)))))

# One object per source and flavour (obj/hw, obj/sim): link.ld places the
# loader's objects by name. Makefile is a prerequisite so a flag change (e.g.
# adding -g, or overriding TC_OPT) forces a rebuild -- otherwise make sees the
# sources unchanged and keeps a stale .elf, and the debugger's C & Assembler
# view would silently show nothing.
vpath %.c $(sort $(dir $(DRV_SRC)))
OBJS       = startup.o stub.o $(notdir $(DRV_SRC:.c=.o)) $(APP_SRC:.c=.o)
OBJ_HW     = $(addprefix obj/hw/,$(OBJS))
OBJ_SIM    = $(addprefix obj/sim/,$(OBJS))

obj/sim/%.o: FLAVOR_CFLAGS = -DSIMULATION $(SIM_FW_DEFINES)
$(OBJ_SIM): $(SIM_STAMP)

COMPILE    = @mkdir -p $(dir $@); echo "  CC  $@"; ${CC} ${CFLAGS} ${DRV_INC} -c $< -o $@
obj/hw/%.o: %.c Makefile $(HDRS)
	$(COMPILE)
obj/sim/%.o: %.c Makefile $(HDRS)
	$(COMPILE)
obj/hw/%.o: %.S Makefile
	$(COMPILE)
obj/sim/%.o: %.S Makefile
	$(COMPILE)

# The relocations stay in the ELF (-q, not loaded): tools/check_loader.py
# checks the loader block reaches nothing below it
READELF    = $(subst objdump,readelf,$(OBJDUMP))
LINK       = ${CC} ${CFLAGS} -Wl,-q -T link.ld $^ -o $@ && \
             { python3 tools/check_loader.py $(READELF) $@ || { rm -f $@; false; }; }
${NAME}.elf: $(OBJ_HW) | link.ld tools/check_loader.py
	$(LINK)
${NAME}_sim.elf: $(OBJ_SIM) | link.ld tools/check_loader.py
	$(LINK)
$(OBJ_HW) $(OBJ_SIM): link.ld

# The logo tables, from the logo demo's generated ones
logo: ../demo_arvern_logo/logo_data.h tools/logo_subset.py
	python3 tools/logo_subset.py $< logo_data.h

%.ihex: %.elf
	${OBJCOPY} -O ihex $^ $@

%.lst: %.elf
	${OBJDUMP} -dSt $^ >$@

%.size: %.elf
	${OBJSIZE}  $^ >$@

#================================================================================================#
#                                                                                                #
#          --- Firmware loader (arvern-tools "arvern-loader" over the JTAG/UART/I2C DTM) ---     #
#                                                                                                #
#================================================================================================#

# --- UART link knobs (host adapter is auto-detected) ---
# PORT is optional: leave it empty and the tool auto-detects the sole USB serial
# port (erroring with a list if several are present). Override when you have more
# than one board attached, e.g.:
#   make run-uart PORT=/dev/cu.usbserial-XXXX
# BAUD is optional too: leave it empty for the tool default (115200) or set it to
# match your board, e.g.:
#   make run-uart BAUD=921600
PORT     ?=
BAUD     ?= 400000
PORT_ARG  = $(if $(PORT),-p $(PORT),)
BAUD_ARG  = $(if $(BAUD),-b $(BAUD),)

# --- I2C link (USB-ISS adapter) knobs ---
# Requires an FPGA built with the I2C DTM (DTM_TYPE=2). I2C_PORT auto-detects the
# sole USB serial port if empty (connect only the USB-ISS). I2C_ADDR is the DTM's
# 7-bit target address (i2c_addr_i strap; reference build = 0x30). I2C_SPEED is the
# SCL bus clock: 100k / 400k / 1000k (higher = faster loads/debug; the DTM keeps up
# past 1 MHz). Example:
#   make run-i2c I2C_PORT=/dev/cu.usbmodemXXXX I2C_ADDR=0x30 I2C_SPEED=400k
I2C_PORT     ?=
I2C_ADDR     ?= 0x30
I2C_SPEED    ?= 1000k
I2C_PORT_ARG  = $(if $(I2C_PORT),-p $(I2C_PORT),)
I2C_ARGS      = --transport i2c $(I2C_PORT_ARG) --i2c-addr $(I2C_ADDR) --i2c-speed $(I2C_SPEED)

# --- UART-over-USB-ISS knobs (USB-ISS in *serial* mode -> a UART DTM board) ---
# One USB-ISS can also drive a UART DTM (DTM_TYPE=1), not just I2C. Reuses the
# USB-ISS port (I2C_PORT, auto-detected if empty) and BAUD (the DTM auto-measures
# whatever the host sends). This target *forces* --adapter usb-iss -- you normally
# don't need it, since plain `run-uart` already auto-detects a plugged USB-ISS.
# Use it only to disambiguate when a USB-ISS AND a plain serial adapter are both
# attached (which would make auto-detect ambiguous).
UART_ISS_ARGS = --transport uart --adapter usb-iss $(I2C_PORT_ARG) $(BAUD_ARG)

# --- JTAG link (FT232H MPSSE) knobs ---
# Requires an FPGA built with the JTAG DTM (DTM_TYPE=0 -- the board default). The
# FT232H is auto-selected by USB id (no port needed). JTAG_FREQ is the TCK clock
# in Hz; the link is robust well past 1 MHz (raise it for faster loads, up to the
# 25 MHz TCK constraint in design.sdc). Example:
#   make run-jtag JTAG_FREQ=15000000
JTAG_FREQ    ?= 1000000
JTAG_ARGS     = --transport jtag --jtag-freq $(JTAG_FREQ)

# Resolve the loader command. Precedence:
#   1. An explicit ARVERN_LOADER=... on the command line / environment.
#   2. "arvern-loader" on PATH (a global `pipx install arvern-tools[elf]`).
#   3. The virtualenv shipped inside the arvern-tools checkout in this repo.
# ARVERN_TOOLS is derived relative to this Makefile so a fresh clone works
# without a global install; override it if your checkout lives elsewhere.
THIS_MK       := $(abspath $(lastword $(MAKEFILE_LIST)))
ARVERN_TOOLS  ?= $(abspath $(dir $(THIS_MK))/../../../../../../arvern-tools)
VENV_LOADER   := $(ARVERN_TOOLS)/.venv/bin/arvern-loader
ARVERN_LOADER ?= $(shell command -v arvern-loader 2>/dev/null || echo $(VENV_LOADER))
VENV_GUI      := $(ARVERN_TOOLS)/.venv/bin/arvern-minidebug
ARVERN_GUI    ?= $(shell command -v arvern-minidebug 2>/dev/null || echo $(VENV_GUI))

# Verify the loader resolves before trying to use it; print install hints if not.
check-loader:
	@command -v "${ARVERN_LOADER}" >/dev/null 2>&1 || { \
	  echo "error: arvern-loader not found (tried: ${ARVERN_LOADER})"; \
	  echo "install it once, either globally:"; \
	  echo "    pipx install --editable '${ARVERN_TOOLS}[elf]'"; \
	  echo "or into the in-repo virtualenv:"; \
	  echo "    python3 -m venv '${ARVERN_TOOLS}/.venv'"; \
	  echo "    '${ARVERN_TOOLS}/.venv/bin/pip' install -e '${ARVERN_TOOLS}[elf]'"; \
	  exit 1; }

# Load the ELF over the DTM, then reset the hart so the firmware runs. The plain
# targets AUTO-DETECT the host adapter (USB-ISS / FTDI / FT232H, by USB id); the
# -usbiss variant just forces it. Flavors:
#   run-jtag        -- JTAG; FT232H                                       (DTM_TYPE=0)
#   run-uart        -- UART; auto-detects USB-ISS or a plain serial/FTDI  (DTM_TYPE=1)
#   run-uart-usbiss -- UART; forces a USB-ISS (only to disambiguate)      (DTM_TYPE=1)
#   run-i2c         -- I2C;  auto-detects a USB-ISS or FT232H             (DTM_TYPE=2)
# A cJTAG build (DTM_TYPE=3) has no target here: use a J-Link (SEGGER Ozone or the
# J-Link GDB Server); see ../../../debug/README.md.
# `run` = run-jtag, matching the board's default DTM_TYPE=0 build.
run: run-jtag

run-jtag: ${NAME}.elf check-loader
	${ARVERN_LOADER} ${JTAG_ARGS} $<

run-uart: ${NAME}.elf check-loader
	${ARVERN_LOADER} ${PORT_ARG} ${BAUD_ARG} $<

run-uart-usbiss: ${NAME}.elf check-loader
	${ARVERN_LOADER} ${UART_ISS_ARGS} $<

run-i2c: ${NAME}.elf check-loader
	${ARVERN_LOADER} ${I2C_ARGS} $<

# Verify the GUI resolves before launching it; print install hints if not.
check-gui:
	@command -v "${ARVERN_GUI}" >/dev/null 2>&1 || { \
	  echo "error: arvern-minidebug not found (tried: ${ARVERN_GUI})"; \
	  echo "install the GUI extra, either globally:"; \
	  echo "    pipx install --editable '${ARVERN_TOOLS}[gui]'"; \
	  echo "or into the in-repo virtualenv:"; \
	  echo "    '${ARVERN_TOOLS}/.venv/bin/pip' install -e '${ARVERN_TOOLS}[gui]'"; \
	  exit 1; }

# Launch the debugger GUI with this app's ELF pre-loaded (opens on the Load tab,
# file already filled in -- pick the port, Connect, and Load). For a clean
# source view rebuild first with a debug opt level, e.g.  make debug TC_OPT=-Og
# Same flavors as run: debug-jtag / debug-uart / debug-uart-usbiss / debug-i2c.
# `debug` = debug-jtag (the GUI runs JTAG at its default TCK). For source-level
# GDB / IDE debugging over JTAG, see ../../../debug (arvern-gdbserver / OpenOCD).
debug: debug-jtag

debug-jtag: ${NAME}.elf check-gui
	${ARVERN_GUI} --transport jtag $<

debug-uart: ${NAME}.elf check-gui
	${ARVERN_GUI} ${PORT_ARG} ${BAUD_ARG} $<

debug-uart-usbiss: ${NAME}.elf check-gui
	${ARVERN_GUI} ${UART_ISS_ARGS} $<

debug-i2c: ${NAME}.elf check-gui
	${ARVERN_GUI} ${I2C_ARGS} $<

.PHONY: all clean logo run run-jtag run-uart run-uart-usbiss run-i2c check-loader \
        debug debug-jtag debug-uart debug-uart-usbiss debug-i2c check-gui

clean:
	rm -rf obj
	rm -f ${NAME}.elf ${NAME}.ihex ${NAME}.lst ${NAME}.size ${NAME}_sim.elf ${NAME}_sim.ihex ${NAME}_sim.lst ${NAME}_sim.size $(SIM_STAMP) sim/sd_image.bin sim/sd_image.mem \
	      sim/app_a.elf sim/app_a.bin sim/app_b.elf sim/app_b.bin
