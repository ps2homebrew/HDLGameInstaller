#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <limits.h>

#include "HDLGManClient.h"
#include "main.h"
#include "system.h"

/* Platform specific functions are located below here. */

#define _WIN32_WINNT 0x500

#include <windows.h>
//#include <Ntddcdrm.h>	//Not available in Microsoft Visual Studio 2010.
#include <winioctl.h>

#ifndef IOCTL_CDROM_GET_DRIVE_GEOMETRY_EX
#define IOCTL_CDROM_GET_DRIVE_GEOMETRY_EX 0x24050
#endif

int SystemDriveGetDiscInfo(void *driveH, u32 *nSectors, u8 *discType){
	DWORD dwNotUsed;
	DISK_GEOMETRY_EX discGeometry;

	if(!DeviceIoControl(driveH, IOCTL_CDROM_GET_DRIVE_GEOMETRY_EX, NULL, 0, &discGeometry, sizeof(discGeometry), &dwNotUsed, NULL)){
		return(-EIO);
	}

	*nSectors=(u32)(discGeometry.DiskSize.QuadPart/discGeometry.Geometry.BytesPerSector);

	if(discGeometry.Geometry.BytesPerSector!=2048){
		return(-1);
	}

#if 0
	if((discGeometry.Geometry.BytesPerSector==2352)){
	    *discType=1; /* Assume that it's a ISO9660 MODE 1/2048 filesystem disc first. */

	}
	else
#endif
	*discType=0xFF; /* The disc has 2048 byte sectors. */

	return(0);
}

void displayAlertMsg(const wchar_t *message, ...){
	wchar_t TextBuffer[128];

	va_list args;
	va_start(args, message);
	vswprintf(TextBuffer, sizeof(TextBuffer) / sizeof(wchar_t), message, args);
	MessageBoxW(NULL, TextBuffer, L"Error", MB_OK|MB_ICONERROR);
	va_end(args);
}

void *openFile(wchar_t *path, int mode){
	DWORD dwDesiredAccess=0, dwCreationDisposition=0, dwShareMode=0;
	void *handle;

	if(mode&O_RDONLY){
		dwDesiredAccess=GENERIC_READ;
		dwCreationDisposition=OPEN_EXISTING;
		dwShareMode=FILE_SHARE_READ;
	}
	if(mode&O_WRONLY){
		dwDesiredAccess=GENERIC_WRITE;
		if(mode&(O_CREAT|O_TRUNC)) dwCreationDisposition=CREATE_ALWAYS;
		else dwCreationDisposition=OPEN_EXISTING;
	}

	handle=CreateFile(path, dwDesiredAccess, dwShareMode, NULL, dwCreationDisposition, FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN, NULL);

	return((handle!=INVALID_HANDLE_VALUE)?handle:NULL);
}

void closeFile(void *handle){
	CloseHandle(handle);
}

int readFile(void *handle, void *buffer, int nbytes){
	u32 numberOfBytesRead;

	return((ReadFile(handle, buffer, nbytes, &numberOfBytesRead, NULL))?nbytes:0);
}

int writeFile(void *handle, void *buffer, int nbytes){
	u32 numberOfBytesRead;

	return((WriteFile(handle, buffer, nbytes, &numberOfBytesRead, NULL))?nbytes:0);
}

s64 seekFile(void *handle, s64 offset, int origin){
	LARGE_INTEGER NewFilePointer, FilePointer;
	BOOL result;

	NewFilePointer.QuadPart=offset;

	result=SetFilePointerEx(handle, NewFilePointer, &FilePointer, origin);

	return((result!=0)?FilePointer.QuadPart:(-1));
}

unsigned long int timemsec(void)
{
	return GetTickCount();
}

unsigned long int diffmsec(unsigned long int d1, unsigned long int d2)
{
	if(d2 < d1)
		return(ULONG_MAX - d1 + d2);
	else
		return(d2 - d1);
}
