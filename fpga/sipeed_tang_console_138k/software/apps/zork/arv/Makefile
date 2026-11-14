# Zork I-III (MojoZork) for the Tang Console 138K
ROOT     = $(abspath ../../../../../../..)
BSP      = ../../../bsp
FATFS    = ../../../lib/fatfs
SPI_SW   = $(ROOT)/arvern-ips/apb_spi/sw
BLNS_SW  = $(ROOT)/arvern-belenos/sw
BLNS_PLATFORM = -I$(abspath ../../../bsp) -DBLNS_PLATFORM_HEADER='"tang_blns.h"'   # the board's Belenos features (belenos_hw.h)
ACLINT_SW = $(ROOT)/arvern-ips/ahb_aclint/sw
I2S_SW   = $(ROOT)/arvern-ips/apb_i2s/sw
GPIO_SW  = $(ROOT)/arvern-ips/apb_gpio/sw
PLIC_SW  = $(ROOT)/arvern-ips/ahb_plic/sw
USB_SW   = $(ROOT)/arvern-ips/apb_usb_host/sw
MZ       = ../mojozork
DK       = ../../../lib/demo_kit

CC       = riscv-none-elf-gcc
SIZE     = riscv-none-elf-size
MARCH    = rv32emc_zbb_zba_zbs_zbc_zicsr_zcb
MABI     = ilp32e
# OUT: image name; FLAVOR: extra defines (-DSIMULATION for the board-level simulation)
OUT     ?= zork
FLAVOR  ?=
O        = obj$(if $(FLAVOR),_sim)
INC      = -I. -I$(MZ) -I$(DK) -I$(FATFS) -I$(BSP) -I$(SPI_SW)/include -I$(SPI_SW)/sd -I$(BLNS_SW)/include $(BLNS_PLATFORM) \
           -I$(ACLINT_SW)/include -I$(I2S_SW)/include -I$(GPIO_SW)/include -I$(PLIC_SW)/include \
           -I$(USB_SW)/include -I$(USB_SW)/hid
BASE     = -march=$(MARCH) -mabi=$(MABI) -g -Os -flto -msave-restore -std=gnu11 -ffunction-sections -fdata-sections \
           -DARV_TANG $(FLAVOR) $(INC) -MMD -MP
CFLAGS   = $(BASE) -Wall -Wno-unused-function
LDFLAGS  = -march=$(MARCH) -mabi=$(MABI) -Os -flto -msave-restore -nostartfiles -T link.ld -Wl,--gc-sections \
           -Wl,-Map,$(OUT).map --specs=nano.specs

# MojoZork is compiled inside zork_main.c (its library calls mapped there)
PORT     = board.c zork_main.c console.c stdio_tang.c libc_tang.c trap.c
OBJS     = $(addprefix $(O)/,$(PORT:.c=.o)) $(O)/startup.o \
           $(O)/ff.o $(O)/sd_diskio.o $(O)/apb_spi.o $(O)/apb_spi_sd.o $(O)/belenos.o $(O)/blns_dpi.o \
           $(O)/blns_ddc.o $(O)/blns_hotplug.o \
           $(O)/blns_gpu.o $(O)/ahb_aclint.o $(O)/led_key_sw.o $(O)/tang_video.o $(O)/apb_i2s.o \
           $(O)/apb_gpio.o $(O)/ahb_plic.o $(O)/apb_usb_host.o $(O)/apb_usb_host_hid.o

$(OUT).elf: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -lm -o $@
	$(SIZE) $@
	riscv-none-elf-objcopy -O ihex $@ $(OUT).ihex

$(O)/zork_main.o: zork_main.c | $(O) ; $(CC) $(CFLAGS) -Wno-unused-variable -Wno-unused-but-set-variable -c $< -o $@
$(O)/%.o: %.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/%.o: $(BLNS_SW)/src/%.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/ahb_aclint.o: $(ACLINT_SW)/src/ahb_aclint.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/led_key_sw.o: $(BSP)/led_key_sw.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/tang_video.o: $(BSP)/tang_video.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/ff.o: $(FATFS)/ff.c | $(O) ; $(CC) $(CFLAGS) -w -c $< -o $@
$(O)/sd_diskio.o: $(FATFS)/sd_diskio.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/apb_spi.o: $(SPI_SW)/src/apb_spi.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/apb_spi_sd.o: $(SPI_SW)/sd/apb_spi_sd.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/apb_i2s.o: $(I2S_SW)/src/apb_i2s.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/apb_gpio.o: $(GPIO_SW)/src/apb_gpio.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/ahb_plic.o: $(PLIC_SW)/src/ahb_plic.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/apb_usb_host.o: $(USB_SW)/src/apb_usb_host.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/apb_usb_host_hid.o: $(USB_SW)/hid/apb_usb_host_hid.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/startup.o: startup.S | $(O) ; $(CC) -march=$(MARCH) -mabi=$(MABI) -c $< -o $@
$(O): ; mkdir -p $(O)
# Each object also depends on the headers it includes (-MMD: $(O)/*.d)
$(OBJS): Makefile
-include $(OBJS:.o=.d)

clean: ; rm -rf obj obj_sim *.elf *.map *.ihex
.PHONY: clean
