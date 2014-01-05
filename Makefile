#
# Makefile
#
PROGNAME = ngtc
CODECDIR = /codecs
BINDIR = /usr/local/bin
SRC = hqv.c codec.c lav.c mpeg.c io.c mp4box.c demuxer.c libx264.c encoder.c log.c vfilter.c

GENARGS = -Wall -D_FILE_OFFSET_BITS=64 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE
STATARGS = -Wl,-z,noexecstack -ffast-math -rdynamic -static -pthread
WINSTATARGS = -Wl,--large-address-aware -lpthread -ffast-math -static -static-libgcc
FLAGS = -Lbuild/lib -Ibuild/include
FLAGS32 = -Lbuild/lib32 -Ibuild/include
FLAGS64 = -Lbuild/lib64 -Ibuild/include

LAVC_LIBS = -lavfilter -lavformat -lavcodec -lswscale -lavutil -lavcore 
BASE_LIBS = -lz -lbz2 -lm -lpthread
EXTRA_LIBS = -lfaac -lmp3lame -lx264 -lpng -lm -lpthread
VP8_LIBS = -lvpx -lvorbis -lvorbisenc -logg

MACHINE = $(shell uname -m)

ifeq ($(MACHINE),x86_64)
FLAGS = $(FLAGS64)
else
endif

ifeq ($(DEBUG),1)
OPT = -g
else
OPT = -O3
endif

ifeq ($(CROSS),1)
OPT := $(OPT) -m32
FLAGS = $(FLAGS32)
endif

SYSTEM = $(shell uname -s)

ifeq ($(SYSTEM),Linux)
OS = -DSYS_LINUX
endif
ifeq ($(SYSTEM),Darwin)
OS = -DSYS_MACOSX
endif
ifeq ($(SYSTEM),CYGWIN_NT-5.1)
OS = 
STATARGS = $(WINSTATARGS)
BASE_LIBS = -lz -lm -lpthread -lcygwin
endif

ifeq ($(STATIC),1)
ARGS = $(STATARGS) $(GENARGS)
else
ARGS = $(GENARGS)
endif

all: ngtc

ngtc:
	./version.sh
	gcc $(OPT) $(ARGS)  \
		-o $(PROGNAME) $(SRC) \
		$(PROGNAME).c $(OS) $(FLAGS) \
		$(LAVC_LIBS) \
		$(BASE_LIBS) \
		$(EXTRA_LIBS)

version:
	./version.sh

install:
	mkdir -p $(CODECDIR)
	cp -f codecs/* $(CODECDIR)/
	strip $(PROGNAME)
	cp -f $(PROGNAME) $(BINDIR)/
	cp -f scripts/process.pl $(BINDIR)/
	cp -f scripts/combine.pl $(BINDIR)/

clean:
	rm -f $(PROGNAME)

