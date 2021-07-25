CFLAGS  += $(shell pkg-config --cflags sdl2 jack)
LDFLAGS += $(shell pkg-config --libs   sdl2 jack) -lportmidi -lm
binaries = keiko midiseq midisine

.PHONY: all clean

# Build mode for project: DEBUG or RELEASE
BUILD_MODE            ?= DEBUG

ifeq ($(BUILD_MODE),DEBUG)
    CFLAGS += -g -pg -O0
endif

all: $(binaries)

clean:
	@rm -f $(binaries) 
