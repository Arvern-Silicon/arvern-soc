# ----------------------------------------------------------------------------
#          _    _           Family:    aRVern System IPs
#         / \__/ \          File:      Makefile (lvgl_demo)
#        /   /\   \         --------------------------------------------
#    ===/   /=========      Copyright: (c) 2026, aRVern-dev
#      /   / RV \   \       Contact:   arvernsilicon@gmail.com
#     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
#
# SPDX-License-Identifier: BSD-3-Clause
# Full license text is available in the LICENSE file at the repository root.
# ----------------------------------------------------------------------------
# LVGL demo for the Tang Console 138K: the board image (lvgl_demo.*) and the
# simulation image (lvgl_demo_sim.*: -DSIMULATION and SIM_FW_DEFINES, e.g.
# SIM_FW_DEFINES=-DLVGL_FLUSH_DIRECT). LVGL (../../lib/lvgl) and the drivers are
# compiled once for both (obj/lib); the application per image (obj/hw,
# obj/sim). DEFS: defines for both images (make clean first).
# ----------------------------------------------------------------------------
NAME      = lvgl_demo
ROOT      = $(abspath ../../../../../..)
BSP       = ../../bsp
LVGL      = ../../lib/lvgl
BLNS_SW   = $(ROOT)/arvern-belenos/sw
BLNS_PLATFORM = -I$(abspath ../../bsp) -DBLNS_PLATFORM_HEADER='"tang_blns.h"'   # the board's Belenos features (belenos_hw.h)
ACLINT_SW = $(ROOT)/arvern-ips/ahb_aclint/sw
USB_SW    = $(ROOT)/arvern-ips/apb_usb_host/sw

CC        = riscv-none-elf-gcc
SIZE      = riscv-none-elf-size
OBJCOPY   = riscv-none-elf-objcopy
MARCH     = rv32emc_zbb_zba_zbs_zbc_zicsr_zcb
MABI      = ilp32e
MAKEFLAGS += -j8

SIM_FW_DEFINES ?=
DEFS      ?=
INC       = -I. -I$(LVGL) -I$(BSP) -I$(BLNS_SW)/include -I$(BLNS_SW)/lvgl $(BLNS_PLATFORM) -I$(ACLINT_SW)/include \
            -I$(USB_SW)/include -I$(USB_SW)/hid
BASE      = -march=$(MARCH) -mabi=$(MABI) -g -Os -flto -msave-restore -std=gnu11 \
            -ffunction-sections -fdata-sections -DLV_CONF_INCLUDE_SIMPLE $(DEFS) $(INC) -MMD -MP
LDFLAGS   = -march=$(MARCH) -mabi=$(MABI) -Os -flto=auto -msave-restore -nostartfiles -T link.ld \
            -Wl,--gc-sections --specs=nano.specs

APP       = main.c board.c
LV_SRCS   = $(shell find $(LVGL)/src -name '*.c') $(wildcard $(LVGL)/*.c)   # with the pruning's stand-ins
DRV_SRCS  = $(BLNS_SW)/lvgl/lv_belenos.c $(BLNS_SW)/src/belenos.c $(BLNS_SW)/src/blns_gpu.c $(BLNS_SW)/src/blns_dpi.c \
            $(BLNS_SW)/src/blns_ddc.c $(BLNS_SW)/src/blns_hotplug.c \
            $(ACLINT_SW)/src/ahb_aclint.c $(BSP)/led_key_sw.c $(BSP)/tang_video.c \
            $(USB_SW)/src/apb_usb_host.c $(USB_SW)/hid/apb_usb_host_hid.c
LV_OBJS   = $(patsubst $(LVGL)/%.c,obj/lib/lvgl/%.o,$(LV_SRCS))
DRV_OBJS  = $(addprefix obj/lib/drv/,$(notdir $(DRV_SRCS:.c=.o)))
LIB_OBJS  = $(LV_OBJS) $(DRV_OBJS) obj/lib/startup.o

all: $(NAME).elf $(NAME)_sim.elf

# The simulation image's defines: a change of SIM_FW_DEFINES rebuilds it
SIM_STAMP := obj/.sim_defines
$(shell mkdir -p obj; printf '%s\n' '$(SIM_FW_DEFINES)' | cmp -s - $(SIM_STAMP) || printf '%s\n' '$(SIM_FW_DEFINES)' > $(SIM_STAMP))

$(NAME).elf: $(addprefix obj/hw/,$(APP:.c=.o)) $(LIB_OBJS) link.ld
	$(CC) $(LDFLAGS) -Wl,-Map,$(NAME).map $(filter %.o,$^) -lm -o $@
	$(SIZE) $@ | tee $(NAME).size
	$(OBJCOPY) -O ihex $@ $(NAME).ihex

$(NAME)_sim.elf: $(addprefix obj/sim/,$(APP:.c=.o)) $(LIB_OBJS) link.ld
	$(CC) $(LDFLAGS) -Wl,-Map,$(NAME)_sim.map $(filter %.o,$^) -lm -o $@
	$(SIZE) $@
	$(OBJCOPY) -O ihex $@ $(NAME)_sim.ihex

obj/hw/%.o: %.c Makefile
	@mkdir -p $(@D)
	$(CC) $(BASE) -Wall -Wextra -c $< -o $@
obj/sim/%.o: %.c Makefile $(SIM_STAMP)
	@mkdir -p $(@D)
	$(CC) $(BASE) -Wall -Wextra -DSIMULATION $(SIM_FW_DEFINES) -c $< -o $@
obj/lib/lvgl/%.o: $(LVGL)/%.c lv_conf.h Makefile
	@mkdir -p $(@D)
	$(CC) $(BASE) -c $< -o $@
obj/lib/startup.o: startup.S
	@mkdir -p $(@D)
	$(CC) -march=$(MARCH) -mabi=$(MABI) -c $< -o $@

define drv_rule
obj/lib/drv/$(notdir $(1:.c=.o)): $(1) Makefile
	@mkdir -p $$(@D)
	$$(CC) $$(BASE) -Wall -c $$< -o $$@
endef
$(foreach s,$(DRV_SRCS),$(eval $(call drv_rule,$(s))))

-include $(shell find obj -name '*.d' 2>/dev/null)

clean:
	rm -rf obj $(NAME).elf $(NAME).ihex $(NAME).map $(NAME).size $(NAME)_sim.elf $(NAME)_sim.ihex $(NAME)_sim.map

.PHONY: all clean
