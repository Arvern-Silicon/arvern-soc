# Prince of Persia port: the hardware and simulation images under the app name
# the Tang scripts expect (prince.*, prince_sim.*); built in arv/. `make data`
# fetches the game data and makes the card's files (tools/fetch_data.py).
# DEFS: extra defines for both images, e.g. DEFS=-DARV_PRESENT_COPY (the
# single displayed frame's present, for comparison; make clean first: the
# objects do not depend on it).
NAME = prince
DEFS ?=
all:
	$(MAKE) -C arv OUT=../$(NAME) DEFS="$(DEFS)"
	$(MAKE) -C arv OUT=../$(NAME)_sim FLAVOR=-DSIMULATION DEFS="$(DEFS)"
data:
	python3 tools/fetch_data.py data
clean:
	$(MAKE) -C arv clean
	rm -f $(NAME).* $(NAME)_sim.*
.PHONY: all data clean
