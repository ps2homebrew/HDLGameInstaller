#include <kernel.h>
#include <errno.h>
#include <libcdvd.h>
#include <libmc.h>
#include <malloc.h>
#include <string.h>
#include <fileXio_rpc.h>

#include "main.h"
#ifdef ENABLE_NETWORK_SUPPORT
#include <ps2ip.h>
#include <netman.h>
#endif

#include "HDLGameList.h"
#include "system.h"
#include "DeviceSupport.h"

static int DevicePollingThreadID=-1, MainThreadID=-1;
static volatile int DevicePollingThreadCommand;
static void *DevicePollingThreadStack=NULL;

enum DEVICE_POLLING_THREAD_COMMANDS{
	DEVICE_POLLING_THREAD_CMD_NONE=0,
	DEVICE_POLLING_THREAD_CMD_STOP
};

static int McUnitStatus[2];
static int MassUnitStatus[1];

static int IsMcUnitReady(int unit){
	int type, free, format, result;

	type=MC_TYPE_NONE;
	if(mcGetInfo(unit, 0, &type, &free, &format)==0){
		mcSync(0, NULL, &result);

		result=(type==MC_TYPE_PS2 && format==MC_FORMATTED)?1:0;
	}
	else result=0;

	return result;
}

static int IsMassUnitReady(int unit){
	return 1;
}

static void DevicePollingThread(void){
	int done;

	done=0;
	while(!done){
		//Process commands.
		if(DevicePollingThreadCommand!=DEVICE_POLLING_THREAD_CMD_NONE){
			if(DevicePollingThreadCommand==DEVICE_POLLING_THREAD_CMD_STOP){
				WakeupThread(MainThreadID);
				done=1;
				continue;
			}

			DevicePollingThreadCommand=DEVICE_POLLING_THREAD_CMD_NONE;
		}

		//Update the status of all units of all devices.
		MassUnitStatus[0]=IsMassUnitReady(0);
		McUnitStatus[0]=IsMcUnitReady(0);
		McUnitStatus[1]=IsMcUnitReady(1);
	}
}

int StartDevicePollingThread(void){
	DevicePollingThreadCommand=DEVICE_POLLING_THREAD_CMD_NONE;
	MainThreadID=GetThreadId();

	DevicePollingThreadStack=malloc(0x800);
	return(DevicePollingThreadID=SysCreateThread(&DevicePollingThread, DevicePollingThreadStack, 0x800, NULL, 0x78));
}

int StopDevicePollingThread(void){
	DevicePollingThreadCommand=DEVICE_POLLING_THREAD_CMD_STOP;
	SleepThread();	//Wait for acknowledgement.

	if(DevicePollingThreadID>=0){
		TerminateThread(DevicePollingThreadID);
		DeleteThread(DevicePollingThreadID);
		DevicePollingThreadID=-1;
	}

	if(DevicePollingThreadStack!=NULL){
		free(DevicePollingThreadStack);
		DevicePollingThreadStack=NULL;
	}

	return 0;
}

int GetIsDeviceUnitReady(const char *device, int unit){
	int result;

	if(strcmp(device, "mc")==0){
		result=McUnitStatus[unit];
	}
	else if(strcmp(device, "mass")==0){
		result=MassUnitStatus[unit];
	}
	else result=-ENODEV;

	return result;
}

