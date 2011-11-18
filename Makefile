TARGET=despot

CC=gcc

ifeq ($(shell uname),Darwin)
CPUARCH ?= $(shell uname -m)
CFLAGS  += -D__APPLE__ -arch $(CPUARCH)
LDFLAGS += -arch $(CPUARCH)

FIX_LIBSPOTIFY = install_name_tool -change @loader_path/../Frameworks/libspotify.framework/libspotify @loader_path/lib/libspotify $@

ifdef USE_AUDIOQUEUE
AUDIO_DRIVER ?= osx
LDFLAGS += -framework AudioToolbox
else
AUDIO_DRIVER ?= openal
LDFLAGS += -framework OpenAL
endif

else
CFLAGS += -D__LINUX__
LDLIBS += -lopenal
AUDIO_DRIVER ?= openal
endif

CFLAGS  += -Wall -Iinclude
LDFLAGS += -Llib
LDLIBS  += -lspotify -lhiredis

ifdef DEBUG
CFLAGS += -ggdb -O0
endif

.PHONY: all clean

all: $(TARGET)

clean:
	rm -f *.o *~ $(TARGET)

$(TARGET): despot.o $(AUDIO_DRIVER)-audio.o audio.o
	$(CC) $^ $(LDFLAGS) $(LDLIBS) -o $@
	$(FIX_LIBSPOTIFY)

audio.o: audio.c audio.h
alsa-audio.o: alsa-audio.c audio.h
dummy-audio.o: dummy-audio.c audio.h
osx-audio.o: osx-audio.c audio.h
openal-audio.o: openal-audio.c audio.h
despot.o: despot.c audio.h
