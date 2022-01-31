EE_BIN = HDLGameInstaller.elf

IRX_PATH = irx

RDB_DEBUG_SUPPORT ?= 0
STRIP ?= 1

EE_RES_OBJS = buttons.o devices.o background.o SJIS2Unicode_bin.o SYSTEM_cnf.o ICON_ico.o BOOT_kelf.o
EE_OBJS = main.o settings.o system.o io.o sjis.o graphics.o UI.o menu.o OSD.o pad.o HDLGameList.o DeviceSupport.o font.o IconLoader.o IconRender.o $(EE_RES_OBJS) $(EE_IOP_OBJS) ipconfig.o HDLGameSvr.o

#IOP modules
EE_IOP_OBJS = IOMANX_irx.o FILEXIO_irx.o SIO2MAN_irx.o MCMAN_irx.o MCSERV_irx.o PADMAN_irx.o POWEROFF_irx.o DEV9_irx.o ATAD_irx.o HDD_irx.o PFS_irx.o HDLFS_irx.o SMAP_irx.o NETMAN_irx.o USBD_irx.o USBHDFSD_irx.o

EE_INCS := -I$(PS2SDK)/ports/include -I$(PS2SDK)/ports/include/freetype2 -I$(PS2DEV)/gsKit/include -I./FreeType -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include
EE_LDFLAGS := -L$(PS2SDK)/ports/lib -L$(PS2DEV)/gsKit/lib -L. -L$(PS2SDK)/ee/lib
EE_LIBS = -lpatches -lgskit -ldmakit -lpng -lzlib -lpadx -lmc -lpoweroff -lfileXio -lcdvd -lhdd -lps2ip -lnetman -lmath3d -lm -lfreetype

EE_GPVAL = -G9760
EE_CFLAGS += -Os -mgpopt $(EE_GPVAL)

EE_TEMP_FILES = IOMANX_irx.c FILEXIO_irx.c SIO2MAN_irx.c MCMAN_irx.c MCSERV_irx.c PADMAN_irx.c POWEROFF_irx.c DEV9_irx.c ATAD_irx.c HDD_irx.c PFS_irx.c HDLFS_irx.c SMAP_irx.c NETMAN_irx.c USBD_irx.c USBHDFSD_irx.c buttons.c devices.c background.c SJIS2Unicode_bin.c SYSTEM_cnf.c ICON_ico.c BOOT_kelf.c

ifeq ($(RDB_DEBUG_SUPPORT),1)
	EE_CFLAGS += -DRDB_DEBUG_SUPPORT=1 -g
	STRIP=0
endif

ifeq ($(STRIP),1)
	EE_LDFLAGS += -s
endif

%.o : %.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

%.o : %.S
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

%.o : %.s
	$(EE_AS) $(EE_ASFLAGS) $< -o $@

$(EE_BIN) : $(EE_OBJS)
	$(EE_CC) $(EE_CFLAGS) $(EE_LDFLAGS) -o $(EE_BIN) $(EE_OBJS) $(EE_LIBS)

all:
	$(MAKE) $(EE_BIN)

clean:
	rm -f $(EE_BIN) $(EE_OBJS) $(EE_TEMP_FILES)

IOMANX_irx.c:
	bin2c $(PS2SDK)/iop/irx/iomanX.irx IOMANX_irx.c IOMANX_irx

FILEXIO_irx.c:
	bin2c $(PS2SDK)/iop/irx/fileXio.irx FILEXIO_irx.c FILEXIO_irx

SIO2MAN_irx.c:
	bin2c $(PS2SDK)/iop/irx/freesio2.irx SIO2MAN_irx.c SIO2MAN_irx

MCMAN_irx.c:
	bin2c $(PS2SDK)/iop/irx/mcman.irx MCMAN_irx.c MCMAN_irx

MCSERV_irx.c:
	bin2c $(PS2SDK)/iop/irx/mcserv.irx MCSERV_irx.c MCSERV_irx

PADMAN_irx.c:
	bin2c $(PS2SDK)/iop/irx/freepad.irx PADMAN_irx.c PADMAN_irx

POWEROFF_irx.c:
	bin2c $(PS2SDK)/iop/irx/poweroff.irx POWEROFF_irx.c POWEROFF_irx

DEV9_irx.c:
	bin2c $(PS2SDK)/iop/irx/ps2dev9.irx DEV9_irx.c DEV9_irx

ATAD_irx.c:
	bin2c $(PS2SDK)/iop/irx/ps2atad.irx ATAD_irx.c ATAD_irx

HDD_irx.c:
	bin2c $(IRX_PATH)/ps2hdd-hdl.irx HDD_irx.c HDD_irx

PFS_irx.c:
	bin2c $(PS2SDK)/iop/irx/ps2fs.irx PFS_irx.c PFS_irx

HDLFS_irx.c:
	bin2c $(IRX_PATH)/hdlfs.irx HDLFS_irx.c HDLFS_irx

SMAP_irx.c:
	bin2c $(PS2SDK)/iop/irx/smap.irx SMAP_irx.c SMAP_irx

NETMAN_irx.c:
	bin2c $(PS2SDK)/iop/irx/netman.irx NETMAN_irx.c NETMAN_irx

USBD_irx.c:
	bin2c $(PS2SDK)/iop/irx/usbd.irx USBD_irx.c USBD_irx

USBHDFSD_irx.c:
	bin2c $(PS2SDK)/iop/irx/usbhdfsd.irx USBHDFSD_irx.c USBHDFSD_irx

SJIS2Unicode_bin.c:
	bin2c res/SJIS2Unicode.bin SJIS2Unicode_bin.c SJIS2Unicode_bin

SYSTEM_cnf.c:
	bin2c res/system.cnf SYSTEM_cnf.c SYSTEM_cnf

ICON_ico.c:
	bin2c res/icon.ico ICON_ico.c ICON_ico

BOOT_kelf.c:
	bin2c res/BOOT.XLF BOOT_kelf.c BOOT_kelf

background.c:
	bin2c resources/background.png background.c background

buttons.c:
	bin2c resources/buttons.png buttons.c buttons

devices.c:
	bin2c resources/devices.png devices.c devices

include $(PS2SDK)/samples/Makefile.pref
