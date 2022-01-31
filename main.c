#include <errno.h>
#include <iopcontrol.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <kernel.h>
#include <libcdvd.h>
#include <libmc.h>
#include <libpad.h>
#include <loadfile.h>
#include <malloc.h>
#include <osd_config.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>
#include <hdd-ioctl.h>
#include <fileXio_rpc.h>
#include <sys/fcntl.h>

#include <gsKit.h>

#include "main.h"
#ifdef ENABLE_NETWORK_SUPPORT
#include <ps2ip.h>
#include <netman.h>
#endif

#include "io.h"
#include "HDLGameList.h"
#include "system.h"
#include "OSD.h"
#include "UI.h"
#include "menu.h"
#include "graphics.h"
#include "settings.h"
#ifdef ENABLE_NETWORK_SUPPORT
#include "HDLGameSvr.h"
#endif

extern unsigned char IOMANX_irx[];
extern unsigned int size_IOMANX_irx;

extern unsigned char SIO2MAN_irx[];
extern unsigned int size_SIO2MAN_irx;

extern unsigned char PADMAN_irx[];
extern unsigned int size_PADMAN_irx;

extern unsigned char MCMAN_irx[];
extern unsigned int size_MCMAN_irx;

extern unsigned char MCSERV_irx[];
extern unsigned int size_MCSERV_irx;

extern unsigned char FILEXIO_irx[];
extern unsigned int size_FILEXIO_irx;

extern unsigned char POWEROFF_irx[];
extern unsigned int size_POWEROFF_irx;

extern unsigned char DEV9_irx[];
extern unsigned int size_DEV9_irx;

extern unsigned char ATAD_irx[];
extern unsigned int size_ATAD_irx;

extern unsigned char HDD_irx[];
extern unsigned int size_HDD_irx;

extern unsigned char PFS_irx[];
extern unsigned int size_PFS_irx;

extern unsigned char HDLFS_irx[];
extern unsigned int size_HDLFS_irx;

extern unsigned char USBD_irx[];
extern unsigned int size_USBD_irx;

extern unsigned char USBHDFSD_irx[];
extern unsigned int size_USBHDFSD_irx;

#ifdef ENABLE_NETWORK_SUPPORT
extern unsigned char SMAP_irx[];
extern unsigned int size_SMAP_irx;

extern unsigned char NETMAN_irx[];
extern unsigned int size_NETMAN_irx;
#endif

extern void *_gp;
extern GSGLOBAL *gsGlobal;
extern GSTEXTURE BackgroundTexture;

struct RuntimeData RuntimeData;

struct SystemInitThreadParam{
	char **argv;
	int SystemInitSema;
	struct RuntimeData *RuntimeData;
};

static void LoadHDDModules(void)
{
	static const char HDD_args[]="-o\0""4""\0""-n\0""128";
	static const char PFS_args[]="-m\0""2""\0""-o\0""4""-n\0""12";

	SifExecModuleBuffer(ATAD_irx, size_ATAD_irx, 0, NULL, NULL);
	SifExecModuleBuffer(HDD_irx, size_HDD_irx, sizeof(HDD_args), HDD_args, NULL);
	SifExecModuleBuffer(PFS_irx, size_PFS_irx, sizeof(PFS_args), PFS_args, NULL);
}


static void SystemInitThread(void *arg){
	SifExecModuleBuffer(MCSERV_irx, size_MCSERV_irx, 0, NULL, NULL);

	if(((struct SystemInitThreadParam*)arg)->RuntimeData->BootDeviceID!=BOOT_DEVICE_HDD){
		LoadHDDModules();
	}

	if(LoadSettings() != 0)
	{
		ImportIPConfigDat();
		SaveSettings();
	}

#ifdef ENABLE_NETWORK_SUPPORT
	SifExecModuleBuffer(NETMAN_irx, size_NETMAN_irx, 0, NULL, NULL);
	SifExecModuleBuffer(SMAP_irx, size_SMAP_irx, 0, NULL, NULL);

	//Initialization will be done in the background. The usability of the network interface will be confirmed later on, with ethValidate().
	ethInit();
#endif

	SifExecModuleBuffer(HDLFS_irx, size_HDLFS_irx, 0, NULL, NULL);

	mcInit(MC_TYPE_XMC);
	NetManInit();

	//Load the game list, so that both the UI and the server can access it immediately after initialization completes.
	LoadHDLGameList(NULL);

	SignalSema(((struct SystemInitThreadParam*)arg)->SystemInitSema);
	ExitDeleteThread();
}

int VBlankStartSema;

static int VBlankStartHandler(int cause){
	ee_sema_t sema;
	iReferSemaStatus(VBlankStartSema, &sema);
	if(sema.count<sema.max_count) iSignalSema(VBlankStartSema);
	ExitHandler();
	return 0;
}

//Terminate all ongoing installations, unmount all unmounted partitions and shutdown the PS2.
/* static void poweroffCallback(void *arg)
{
	printf("Power button pressed.\n");

	//Close all files and unmount all partitions.
#ifdef ENABLE_NETWORK_SUPPORT
	DeinitializeServer();
#endif
	//TODO: terminate any installations via CD/DVD.

	//Shut down DEV9
	fileXioDevctl("dev9x:", DDIOC_OFF, NULL, 0, NULL, 0);

	poweroffShutdown();
} */

#define SYSTEM_INIT_THREAD_STACK_SIZE	0x800

static void DeinitServices(void)
{
	sceCdInit(SCECdEXIT);

	DeinitPads();

	DeleteSema(RuntimeData.InstallationLockSema);
	DisableIntc(kINTC_VBLANK_START);
	RemoveIntcHandler(kINTC_VBLANK_START, 0);
	DeleteSema(VBlankStartSema);

	//pfs0 is mounted, if booted from the HDD. Do not check on the return value because it will not be mounted, otherwise.
	fileXioUmount("pfs0:");
	fileXioExit();

	SifExitRpc();
}

int main(int argc, char *argv[])
{
	ee_sema_t ThreadSema;
	struct SystemInitThreadParam InitThreadParam;
	unsigned int FrameNum;
	void *SysInitThreadStack;
	char BlockDevice[16], *FullPath[256];
	const char *MountPoint;
	int result;

#ifdef RDB_DEBUG_SUPPORT
	chdir("mass:/HDLGameInstaller/");
#endif
	if((RuntimeData.BootDeviceID=GetBootDeviceID())==BOOT_DEVICE_UNKNOWN){
		return ENODEV;
	}

	SifInitRpc(0);
#ifndef RDB_DEBUG_SUPPORT
	while(!SifIopReset("", 0)){};
#endif

	/* Do as many things as possible as the IOP slowly resets itself. */
	InitOSDResourceFiles();

	RuntimeData.ReadMode.trycount = 255;
	RuntimeData.ReadMode.spindlctrl = SCECdSpinMax;
	RuntimeData.ReadMode.datapattern = SCECdSecS2048;
	RuntimeData.ReadMode.pad = 0;

	InitThreadParam.argv=argv;
	InitThreadParam.RuntimeData=&RuntimeData;

	ThreadSema.init_count=0;
	ThreadSema.max_count=1;
	ThreadSema.attr=0;
	ThreadSema.option=(u32)"InitThreadParam";
	InitThreadParam.SystemInitSema=CreateSema(&ThreadSema);

	ThreadSema.init_count=1;
	ThreadSema.max_count=1;
	ThreadSema.attr=0;
	ThreadSema.option=(u32)"Install-Lock";
	RuntimeData.InstallationLockSema=CreateSema(&ThreadSema);

	SysInitThreadStack=malloc(SYSTEM_INIT_THREAD_STACK_SIZE);

	InitCentralHDLGameList();

	ThreadSema.init_count=0;
	ThreadSema.max_count=1;
	ThreadSema.attr=0;
	ThreadSema.option=(u32)"VBlank";
	VBlankStartSema=CreateSema(&ThreadSema);

	AddIntcHandler(kINTC_VBLANK_START, &VBlankStartHandler, 0);
	EnableIntc(kINTC_VBLANK_START);

	LoadDefaults();

	while(!SifIopSync()){};

	SifInitRpc(0);
	SifInitIopHeap();
	SifLoadFileInit();

	sbv_patch_enable_lmb();

	SifExecModuleBuffer(IOMANX_irx, size_IOMANX_irx, 0, NULL, NULL);
	SifExecModuleBuffer(FILEXIO_irx, size_FILEXIO_irx, 0, NULL, NULL);
	fileXioInit();
	fileXioSetRWBufferSize(FILEIO_BLOCK_SIZE);

	SifExecModuleBuffer(USBD_irx, size_USBD_irx, 0, NULL, NULL);
	SifExecModuleBuffer(USBHDFSD_irx, size_USBHDFSD_irx, 0, NULL, NULL);

	SifExecModuleBuffer(SIO2MAN_irx, size_SIO2MAN_irx, 0, NULL, NULL);
	SifExecModuleBuffer(MCMAN_irx, size_MCMAN_irx, 0, NULL, NULL);
	SifExecModuleBuffer(PADMAN_irx, size_PADMAN_irx, 0, NULL, NULL);

	SifExecModuleBuffer(POWEROFF_irx, size_POWEROFF_irx, 0, NULL, NULL);
#ifndef RDB_DEBUG_SUPPORT
	SifExecModuleBuffer(DEV9_irx, size_DEV9_irx, 0, NULL, NULL);
#endif

	sceCdInit(SCECdINoD);
	InitPads();

	//Initialize poweroff library.
	poweroffInit();

	//Set poweroff callback function
	//For now, we have no callback. Not setting a callback will also disable automatic poweroff.
	//poweroffSetCallback(&poweroffCallback, NULL);

	if(RuntimeData.BootDeviceID == BOOT_DEVICE_HDD)
		LoadHDDModules();

	SysCreateThread(SystemInitThread, SysInitThreadStack, SYSTEM_INIT_THREAD_STACK_SIZE, &InitThreadParam, 2);

	if(SysBootDeviceInit() != 0)
	{
		WaitSema(InitThreadParam.SystemInitSema);
		DeinitServices();
		return -1;
	}

	if(InitializeUI(0) != 0)
	{
		DeinitializeUI();

		WaitSema(InitThreadParam.SystemInitSema);
		DeinitServices();
		return -1;
	}

	FrameNum=0;
	/* Draw something nice here while waiting... */
	do{
		RedrawLoadingScreen(FrameNum);
		FrameNum++;
	}while(PollSema(InitThreadParam.SystemInitSema)!=InitThreadParam.SystemInitSema);
	DeleteSema(InitThreadParam.SystemInitSema);
	free(SysInitThreadStack);

	SifLoadFileExit();
	SifExitIopHeap();

	DisplayFlashStatusUpdate(SYS_UI_MSG_CONNECTING);
#ifdef ENABLE_NETWORK_SUPPORT
	ethValidate();
	InitializeStartServer();
#endif

	MainMenu();

#ifdef ENABLE_NETWORK_SUPPORT
	DeinitializeServer();
#endif

	DeinitCentralHDLGameList();

	DeinitializeUI();
	DeinitServices();

	return 0;
}
