# relaunch.prx: modulo de kernel que vuelve a lanzar el EBOOT del juego (selector de mapas). Desde un juego de la
# Memory Stick la consola no deja relanzarse en modo usuario (sceKernelLoadExec): desde kernel, con la funcion del
# CFW (SystemCtrl: PRO, ME, ARK), como hacen sus menus al abrir un EBOOT
TARGET = relaunch
OBJS = main.o
BUILD_PRX = 1
USE_KERNEL_LIBC = 1
USE_KERNEL_LIBS = 1
CFLAGS = -O2 -G0 -Wall
LIBS = -lpspsystemctrl_kernel
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
