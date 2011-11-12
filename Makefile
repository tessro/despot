TARGET=despot

CC=gcc

ifeq ($(shell uname),Darwin)
ifdef USE_AUDIOQUEUE
AUDIO_DRIVER ?= osx
LDFLAGS += -framework AudioToolbox
else
AUDIO_DRIVER ?= openal
LDFLAGS += -framework OpenAL
endif
else
CFLAGS  = $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags alsa)
LDFLAGS = $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs-only-L alsa)
LDLIBS  = $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs-only-l --libs-only-other alsa)
AUDIO_DRIVER ?= alsa
endif

CFLAGS  += -Wall -Iinclude
LDFLAGS += -Llib
LDLIBS  += -lspotify -lhiredis

ifeq ($(shell uname),Darwin)
CPUARCH   ?= $(shell uname -m)
CFLAGS    += -D__APPLE__ -arch $(CPUARCH)
LDFLAGS   += -arch $(CPUARCH)
endif

ifdef DEBUG
CFLAGS += -ggdb -O0
endif

.PHONY: all clean

all: $(TARGET)

clean:
	rm -f *.o *~ $(TARGET) searchplay

$(TARGET): despot.o $(AUDIO_DRIVER)-audio.o audio.o
	$(CC) $^ $(LDFLAGS) $(LDLIBS) -o $@
	install_name_tool -change @loader_path/../Frameworks/libspotify.framework/libspotify @loader_path/lib/libspotify $@

audio.o: audio.c audio.h
alsa-audio.o: alsa-audio.c audio.h
dummy-audio.o: dummy-audio.c audio.h
osx-audio.o: osx-audio.c audio.h
openal-audio.o: openal-audio.c audio.h
despot.o: despot.c audio.h
