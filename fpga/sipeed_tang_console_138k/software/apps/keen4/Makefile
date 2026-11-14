# Commander Keen 4 port: the hardware and simulation images under the app name
# the Tang scripts expect (keen4.*, keen4_sim.*); built in arv/
NAME = keen4
all:
	$(MAKE) -C arv OUT=../$(NAME)
	$(MAKE) -C arv OUT=../$(NAME)_sim FLAVOR=-DSIMULATION
clean:
	$(MAKE) -C arv clean
	rm -f $(NAME).* $(NAME)_sim.*
.PHONY: all clean
