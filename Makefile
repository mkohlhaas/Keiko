CFLAGS  += $(shell pkg-config --cflags sdl2 jack)
LDFLAGS += $(shell pkg-config --libs   sdl2 jack) -lportmidi
binaries = keiko midiseq

.PHONY: all clean

all: $(binaries)

clean:
	@rm -f $(binaries) 
