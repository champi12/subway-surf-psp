# Subway Surfers para PSP - pspdev / PSPSDK
TARGET = subway
OBJS = src/main.o src/data.o src/render.o src/scene.o src/spawn.o src/physics.o src/anim.o src/audio.o \
       src/game.o src/hud.o src/missions.o

INCDIR = src
CFLAGS = -O2 -G0 -Wall -ffast-math -fno-strict-aliasing
CFLAGS += $(EXTRA)
ifdef AUTOTEST
CFLAGS += -DAUTOTEST=$(AUTOTEST)
endif
ASFLAGS = $(CFLAGS)
LIBS = -lpspgum -lpspgu -lpspmp3 -lpspatrac3 -lpspaudio -lpsputility -lpsppower -lm

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Subway Surfers
ifneq ($(wildcard ICON0.PNG),)
PSP_EBOOT_ICON = ICON0.PNG
endif

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
