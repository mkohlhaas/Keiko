.PHONY: all clean

all: keiko

keiko: keiko.c
	gcc $$(pkg-config --cflags --libs sdl2) -I/usr/include -L/usr/lib -lportmidi -o $@ $<

clean:
	@rm -f keiko
