# Commander Keen 4 (Omnispeak) for the Tang Console 138K
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
OMNI     = ../omnispeak/src

CC       = riscv-none-elf-gcc
SIZE     = riscv-none-elf-size
MARCH    = rv32emc_zbb_zba_zbs_zbc_zicsr_zcb
MABI     = ilp32e
# OUT: image name; FLAVOR: extra defines (-DSIMULATION for the board-level simulation)
OUT     ?= keen4
FLAVOR  ?=
O        = obj$(if $(FLAVOR),_sim)
INC      = -I. -I$(OMNI) -I$(FATFS) -I$(BSP) -I$(SPI_SW)/include -I$(SPI_SW)/sd -I$(BLNS_SW)/include $(BLNS_PLATFORM) \
           -I$(ACLINT_SW)/include -I$(I2S_SW)/include -I$(GPIO_SW)/include -I$(PLIC_SW)/include \
           -I$(USB_SW)/include -I$(USB_SW)/hid
# -flto and -msave-restore (the prologues and epilogues shared): the code fits the 160 KB ROM
BASE     = -march=$(MARCH) -mabi=$(MABI) -g -Os -flto -msave-restore -std=gnu99 -ffunction-sections -fdata-sections \
           -DWITH_KEEN4=1 -DVANILLA=1 -DARV_TANG -DARV_LINKED_VARS $(FLAVOR) $(INC) -MMD -MP
# The engine's printf family, puts and exit: the small ones of libc_tang.c
LIBC     = -Dprintf=arv_printf -Dvprintf=arv_vprintf -Dfprintf=arv_fprintf -Dvfprintf=arv_vfprintf \
           -Dsprintf=arv_sprintf -Dsnprintf=arv_snprintf -Dvsnprintf=arv_vsnprintf -Dputs=arv_puts_nl \
           -Dexit=arv_exit
CFLAGS   = $(BASE) -Wall -Wno-unused-function
ENGINE_CFLAGS = $(BASE) $(LIBC) -w -Wno-error=implicit-function-declaration -Wno-error=int-conversion \
           -Wno-error=incompatible-pointer-types
LDFLAGS  = -march=$(MARCH) -mabi=$(MABI) -Os -flto -msave-restore -nostartfiles -T link.ld -Wl,--gc-sections \
           -Wl,-Map,$(OUT).map --specs=nano.specs

# The engine without its file layer, memory manager and null back-ends
# (fs_tang.c, mm_tang.c and the *_tang.c back-ends stand in)
ENGINE   = id_ca.c id_cfg.c id_in.c id_rf.c id_sd.c id_str.c id_ti.c id_us_1.c id_us_2.c \
           id_us_textscreen.c id_vh.c id_vl.c ck4_map.c ck4_misc.c ck4_obj1.c ck4_obj2.c ck4_obj3.c \
           ck_act.c ck_cross.c ck_game.c ck_inter.c ck_keen.c ck_main.c ck_map.c ck_misc.c ck_obj.c \
           ck_phys.c ck_play.c ck_quit.c ck_text.c
PORT     = board.c gfx.c vl_tang.c gfxcache.c mm_tang.c fs_tang.c sd_tang.c in_tang.c audio.c \
           libc_tang.c trap.c keen4_vars.c
OBJS     = $(addprefix $(O)/k_,$(ENGINE:.c=.o)) $(addprefix $(O)/,$(PORT:.c=.o)) $(O)/startup.o \
           $(O)/ff.o $(O)/sd_diskio.o $(O)/apb_spi.o $(O)/apb_spi_sd.o $(O)/belenos.o $(O)/blns_dpi.o \
           $(O)/blns_ddc.o $(O)/blns_hotplug.o \
           $(O)/blns_gpu.o $(O)/ahb_aclint.o $(O)/led_key_sw.o $(O)/tang_video.o $(O)/apb_i2s.o \
           $(O)/apb_gpio.o $(O)/ahb_plic.o $(O)/apb_usb_host.o $(O)/apb_usb_host_hid.o

$(OUT).elf: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@
	riscv-none-elf-objcopy -O ihex $@ $(OUT).ihex

$(O)/k_%.o: $(OMNI)/%.c | $(O) ; $(CC) $(ENGINE_CFLAGS) -c $< -o $@
$(O)/k_ck_main.o: $(OMNI)/ck_main.c | $(O) ; $(CC) $(ENGINE_CFLAGS) -Dmain=keen_main -c $< -o $@
$(O)/%.o: %.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/keen4_vars.o: keen4_vars.c | $(O) ; $(CC) $(ENGINE_CFLAGS) -c $< -o $@
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
