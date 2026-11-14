# Prince of Persia (SDLPoP, picopop's fork) for the Tang Console 138K
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
POP      = ../sdlpop
PSDL     = ../picosdl

CC       = riscv-none-elf-gcc
SIZE     = riscv-none-elf-size
MARCH    = rv32emc_zbb_zba_zbs_zbc_zicsr_zcb
MABI     = ilp32e
# OUT: image name; FLAVOR: extra defines (-DSIMULATION for the board-level simulation);
# DEFS: more defines (-DARV_PRESENT_COPY: one displayed frame, psdl_tang.c)
OUT     ?= prince
FLAVOR  ?=
DEFS    ?=
O        = obj$(if $(FLAVOR),_sim)
INC      = -I. -I$(POP) -I$(PSDL)/include -I$(PSDL)/src -I$(FATFS) -I$(BSP) -I$(SPI_SW)/include -I$(SPI_SW)/sd -I$(BLNS_SW)/include $(BLNS_PLATFORM) \
           -I$(ACLINT_SW)/include -I$(I2S_SW)/include -I$(GPIO_SW)/include -I$(PLIC_SW)/include \
           -I$(USB_SW)/include -I$(USB_SW)/hid
BASE     = -march=$(MARCH) -mabi=$(MABI) -g -Os -flto -msave-restore -std=gnu11 -ffunction-sections -fdata-sections \
           -DARV_TANG -DPICOPOP=1 -DPICOPOP_ONE_SCREEN_BUFFER=1 -DPSDL_HAVE_AUDIO=1 -DPSDL_ARENA_BYTES=8192 -DPSDL_ARENA_EXTERN -DPSDL_BACKEND_GPU -DPSDL_EVENT_QUEUE_LEN=32 \
           $(FLAVOR) $(DEFS) $(INC) -MMD -MP
# The game's printf family: libc_tang.c and stdio_tang.c
LIBC     = -Dprintf=arv_printf -Dsprintf=arv_sprintf -Dsnprintf=arv_snprintf -Dvsnprintf=arv_vsnprintf \
           -Dfprintf=arv_fprintf -Dvfprintf=arv_vfprintf -Dputs=arv_puts_nl -Dexit=arv_exit
CFLAGS   = $(BASE) -Wall -Wno-unused-function
GAME_CFLAGS = $(BASE) $(LIBC) -w
LDFLAGS  = -march=$(MARCH) -mabi=$(MABI) -Os -flto -msave-restore -nostartfiles -T link.ld -Wl,--gc-sections \
           -Wl,-Map,$(OUT).map --specs=nano.specs

# The game (its resources from the card: pak.c) and picosdl (its backend: psdl_tang.c)
GAME     = data.c seg000.c seg001.c seg002.c seg003.c seg004.c seg005.c seg006.c seg007.c seg008.c \
           seg009.c seqtbl.c options.c replay.c lighting.c screenshot.c menu.c
PSDLSRC  = $(notdir $(wildcard $(PSDL)/src/*.c))
PORT     = board.c pak.c psdl_tang.c music.c audio.c stdio_tang.c libc_tang.c trap.c
OBJS     = $(addprefix $(O)/g_,$(GAME:.c=.o)) $(addprefix $(O)/s_,$(PSDLSRC:.c=.o)) \
           $(addprefix $(O)/,$(PORT:.c=.o)) $(O)/startup.o \
           $(O)/ff.o $(O)/sd_diskio.o $(O)/apb_spi.o $(O)/apb_spi_sd.o $(O)/belenos.o $(O)/blns_dpi.o \
           $(O)/blns_ddc.o $(O)/blns_hotplug.o \
           $(O)/blns_gpu.o $(O)/ahb_aclint.o $(O)/led_key_sw.o $(O)/tang_video.o $(O)/apb_i2s.o \
           $(O)/apb_gpio.o $(O)/ahb_plic.o $(O)/apb_usb_host.o $(O)/apb_usb_host_hid.o

$(OUT).elf: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -lm -o $@
	$(SIZE) $@
	riscv-none-elf-objcopy -O ihex $@ $(OUT).ihex

# Each object also depends on the headers it includes (-MMD: $(O)/*.d)
$(O)/g_%.o: $(POP)/%.c | $(O) ; $(CC) $(GAME_CFLAGS) -c $< -o $@
$(O)/s_%.o: $(PSDL)/src/%.c | $(O) ; $(CC) $(GAME_CFLAGS) -c $< -o $@
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
$(OBJS): Makefile
-include $(OBJS:.o=.d)

clean: ; rm -rf obj obj_sim *.elf *.map *.ihex
.PHONY: clean
