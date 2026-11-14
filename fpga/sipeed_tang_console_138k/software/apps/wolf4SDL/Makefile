# Wolf3D port: the hardware and simulation images under the app name the
# Tang scripts expect (wolf4SDL.*, wolf4SDL_sim.*); built in arv/
NAME = wolf4SDL
all:
	$(MAKE) -C arv OUT=../$(NAME)
	$(MAKE) -C arv OUT=../$(NAME)_sim FLAVOR=-DSIMULATION
clean:
	$(MAKE) -C arv clean
	rm -f $(NAME).* $(NAME)_sim.*
# TABLES.BIN for a WOLF_SD_TABLES build (tables read from the card instead of ROM:
# saves 6 KB of ROM, costs 6 KB of heap; off, the heap is too tight)
data/TABLES.BIN: port/wolf_tables.c tools/gen_tables_bin.py
	python3 tools/gen_tables_bin.py $< $@
.PHONY: all clean
