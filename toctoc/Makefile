.PHONY: all
all: toctoc

.PHONY: clean
clean:
	rm toctoc

toctoc: toctoc.c
	cc -Wall -Werror -Wextra -pedantic -o $@ $<
