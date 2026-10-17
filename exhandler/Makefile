# exhandler.prx: modulo de kernel que captura las excepciones de la CPU y escribe crash.txt
TARGET = exhandler
OBJS = main.o
BUILD_PRX = 1
USE_KERNEL_LIBC = 1
USE_KERNEL_LIBS = 1
CFLAGS = -O2 -G0 -Wall
LIBS = -lpspdebug -lpspdisplay_driver -lpspge_driver -lpspctrl_driver
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
