.PHONY: all

all:
	gcc $$(pkg-config --cflags --libs sdl2) -I/usr/include -L/usr/lib -lportmidi -o keiko keiko.c
