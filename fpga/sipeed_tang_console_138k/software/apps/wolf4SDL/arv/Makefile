# Wolf4SDL for the Tang Console 138K
ROOT     = $(abspath ../../../../../../..)
BSP      = ../../../bsp
SPI_SW   = $(ROOT)/arvern-ips/apb_spi/sw
FATFS    = ../third_party/fatfs/source
BLNS_SW  = $(ROOT)/arvern-belenos/sw
BLNS_PLATFORM = -I$(abspath ../../../bsp) -DBLNS_PLATFORM_HEADER='"tang_blns.h"'   # the board's Belenos features (belenos_hw.h)
ACLINT_SW = $(ROOT)/arvern-ips/ahb_aclint/sw
I2S_SW   = $(ROOT)/arvern-ips/apb_i2s/sw
GPIO_SW  = $(ROOT)/arvern-ips/apb_gpio/sw
PLIC_SW  = $(ROOT)/arvern-ips/ahb_plic/sw
USB_SW   = $(ROOT)/arvern-ips/apb_usb_host/sw
WOLF     = ../Wolf4SDL
SDL_INC ?= /opt/homebrew/include/SDL2

CC       = riscv-none-elf-gcc
SIZE     = riscv-none-elf-size
MARCH    = rv32emc_zbb_zba_zbs_zbc_zicsr_zcb
MABI     = ilp32e
VERSION  = -DVERSIONALREADYCHOSEN -DCARMACIZED -DUPLOAD -DWOLF_NO_SIGNON -DWOLF_NO_FLOAT -DWOLF_PAGE_CACHE -DWOLF_SHIFTS_ON_DEMAND -DWOLF_MAP_TWO_PLANES -DWOLF_PLATFORM_MAPS -DWOLF_NO_ADLIB_MUSIC -DWOLF_NO_DIGI -DWOLF_GR_CACHE -DWOLF_HEAP_REPORT -DWOLF_GPU_CLEAR -DWOLF_GPU_WALLS -DWOLF_VSYNC -DWOLF_FIXEDMUL_INLINE -DWOLF_GPU_SPRITES -DWOLF_STREAM_MUSIC -DWOLF_STREAM_SFX
# OUT: image name; FLAVOR: extra defines (-DSIMULATION for the board-level simulation)
OUT     ?= wolf3d
FLAVOR  ?=
O        = obj$(if $(FLAVOR),_sim)
CFLAGS   = -march=$(MARCH) -mabi=$(MABI) -g -Os -std=c99 -D_POSIX_C_SOURCE=200809L -w \
           -ffunction-sections -fdata-sections $(VERSION) $(FLAVOR) -MMD -MP \
           -I. -I../stub -I$(SDL_INC) -I$(WOLF) -I$(FATFS) -I$(BSP) -I$(SPI_SW)/include -I$(SPI_SW)/sd -I$(BLNS_SW)/include $(BLNS_PLATFORM) -I$(ACLINT_SW)/include \
           -I$(I2S_SW)/include -I$(GPIO_SW)/include -I$(PLIC_SW)/include -I$(USB_SW)/include -I$(USB_SW)/hid
LDFLAGS  = -march=$(MARCH) -mabi=$(MABI) -nostartfiles -T link.ld -Wl,--gc-sections \
           -Wl,-Map,$(OUT).map --specs=nano.specs

# Reductions: no OPL emulator (silent stand-in), no sign-on image, no debug keys
WOLF_SRC = id_ca.c id_in.c id_pm.c id_sd.c id_us.c id_vh.c id_vl.c \
           wl_act1.c wl_act2.c wl_agent.c wl_atmos.c wl_cloudsky.c wl_draw.c \
           wl_game.c wl_inter.c wl_main.c wl_menu.c wl_parallax.c wl_plane.c wl_play.c \
           wl_scale.c wl_shade.c wl_state.c wl_text.c wl_utils.c
OBJS     = $(addprefix $(O)/w_,$(notdir $(WOLF_SRC:.c=.o))) $(O)/ff.o $(O)/ffunicode.o \
           $(O)/apb_spi.o $(O)/apb_spi_sd.o $(O)/diskio.o $(O)/syscalls.o $(O)/startup.o \
           $(O)/opl_silent.o $(O)/no_debug.o $(O)/wolf_tables.o $(O)/wolf_atan2.o $(O)/platform.o \
           $(O)/sdl_tang.o $(O)/belenos.o $(O)/blns_dpi.o $(O)/blns_gpu.o $(O)/ahb_aclint.o $(O)/led_key_sw.o $(O)/tang_video.o \
           $(O)/blns_ddc.o $(O)/blns_hotplug.o \
           $(O)/audio.o $(O)/apb_i2s.o $(O)/apb_gpio.o $(O)/ahb_plic.o $(O)/apb_usb_host.o $(O)/apb_usb_host_hid.o

$(OUT).elf: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@
	riscv-none-elf-objcopy -O ihex $@ $(OUT).ihex

$(O)/w_%.o: $(WOLF)/%.c | $(O) ; $(CC) $(CFLAGS) -Dfopen=arv_fopen -Dfread=arv_fread -c $< -o $@
# The ray caster and the wall posts (WallRefresh, HitVertWall/HitHorizWall,
# CalcHeight, ScalePost): -O2, the rest -Os
$(O)/w_wl_draw.o: CFLAGS := $(subst -Os,-O2,$(CFLAGS))
$(O)/w_wl_main.o: $(WOLF)/wl_main.c | $(O) ; $(CC) $(CFLAGS) -Dfopen=arv_fopen -Dfread=arv_fread -Dmain=wolf_main -c $< -o $@
$(O)/%.o: $(BLNS_SW)/src/%.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/ahb_aclint.o: $(ACLINT_SW)/src/ahb_aclint.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/led_key_sw.o: $(BSP)/led_key_sw.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/tang_video.o: $(BSP)/tang_video.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/ff.o: $(FATFS)/ff.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/ffunicode.o: $(FATFS)/ffunicode.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/apb_spi.o: $(SPI_SW)/src/apb_spi.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/apb_i2s.o: $(I2S_SW)/src/apb_i2s.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/apb_gpio.o: $(GPIO_SW)/src/apb_gpio.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/ahb_plic.o: $(PLIC_SW)/src/ahb_plic.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/apb_usb_host.o: $(USB_SW)/src/apb_usb_host.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/apb_usb_host_hid.o: $(USB_SW)/hid/apb_usb_host_hid.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/apb_spi_sd.o: $(SPI_SW)/sd/apb_spi_sd.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/%.o: %.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/%.o: ../port/%.c | $(O) ; $(CC) $(CFLAGS) -c $< -o $@
$(O)/startup.o: startup.S | $(O) ; $(CC) -march=$(MARCH) -mabi=$(MABI) -c $< -o $@
$(O): ; mkdir -p $(O)
# Each object also depends on the headers it includes (-MMD: $(O)/*.d)
$(OBJS): Makefile
-include $(OBJS:.o=.d)

clean: ; rm -rf obj obj_sim *.elf *.map *.ihex
