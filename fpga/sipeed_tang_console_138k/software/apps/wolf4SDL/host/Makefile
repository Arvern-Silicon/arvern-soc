# Host (macOS) reference build of Wolf4SDL: shareware data, no audio,
# misaligned-access checks (the aRVern core traps on them)
WOLF     = ../Wolf4SDL
# Same reductions as the target: no OPL emulator, no sign-on image, no debug keys
SRCS     = id_ca.c id_in.c id_pm.c id_sd.c id_us.c id_vh.c id_vl.c \
           wl_act1.c wl_act2.c wl_agent.c wl_atmos.c wl_cloudsky.c wl_draw.c \
           wl_game.c wl_inter.c wl_main.c wl_menu.c wl_parallax.c wl_plane.c wl_play.c \
           wl_scale.c wl_shade.c wl_state.c wl_text.c wl_utils.c
CFLAGS   = -O1 -g -std=gnu99 -w -DVERSIONALREADYCHOSEN -DCARMACIZED -DUPLOAD -DWOLF_NO_SIGNON -DWOLF_NO_FLOAT -DWOLF_PAGE_CACHE -DWOLF_SHIFTS_ON_DEMAND -DWOLF_MAP_TWO_PLANES -DWOLF_PLATFORM_MAPS -DWOLF_NO_ADLIB_MUSIC -DWOLF_NO_DIGI -DWOLF_GR_CACHE \
           -fsanitize=alignment -fno-sanitize-recover=alignment \
           $(shell sdl2-config --cflags) -I../stub -I$(WOLF)
wolf3d_host: $(addprefix $(WOLF)/,$(SRCS)) ../port/opl_silent.c ../port/no_debug.c ../port/wolf_tables.c ../port/wolf_atan2.c mixer_stub.c
	cc $(CFLAGS) -Dmalloc=wolf_malloc -Dfree=wolf_free $(addprefix $(WOLF)/,$(SRCS)) ../port/opl_silent.c ../port/no_debug.c ../port/wolf_tables.c ../port/wolf_atan2.c -c
	cc $(CFLAGS) mixer_stub.c *.o $(shell sdl2-config --libs) -fsanitize=alignment -Wl,-ld_classic -o $@
clean: ; rm -f wolf3d_host
