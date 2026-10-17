# exloader.prx: modulo de usuario que carga exhandler.prx con kubridge (sin el arranque de pspsdk)
TARGET = exloader
OBJS = main.o
BUILD_PRX = 1
CFLAGS = -O2 -G0 -Wall
LDFLAGS = -nostartfiles
LIBS = -lpspkubridge
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
