all: DellDevice

DellDevice: main.c
	gcc -o $@  $<

clean:
	rm -f DellDevice

.PHONY: all clean
