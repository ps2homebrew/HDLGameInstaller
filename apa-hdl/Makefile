# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2001-2004, ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.

LIBAPA_PATH = $(PS2SDKSRC)/iop/hdd/libapa/

LIBAPA_SRC_DIR = $(LIBAPA_PATH)src/

IOP_BIN ?= ps2hdd-hdl.irx

IOP_INCS += -I$(PS2SDKSRC)/iop/dev9/poweroff/include -I$(PS2SDKSRC)/iop/dev9/dev9/include -I$(PS2SDKSRC)/iop/dev9/atad/include -I$(LIBAPA_PATH)include
IOP_CFLAGS += -DAPA_OSD_VER

APA_OBJS = password.o misc.o cache.o apa.o journal.o free.o
IOP_OBJS = hdd_fio.o hdd.o imports.o $(APA_OBJS)

include $(PS2SDKSRC)/Defs.make
include $(PS2SDKSRC)/iop/Rules.bin.make
include $(PS2SDKSRC)/iop/Rules.make
include $(PS2SDKSRC)/iop/Rules.release

$(IOP_OBJS_DIR)%.o: $(LIBAPA_SRC_DIR)%.c
	$(IOP_CC) $(IOP_CFLAGS) -c $< -o $@
