#include <errno.h>
#include <iopheap.h>
#include <kernel.h>
#include <libcdvd.h>
#include <libpad.h>
#include <libmc.h>
#include <limits.h>
#include <loadfile.h>
#include <malloc.h>
#include <osd_config.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>
#include <timer.h>
#include <wchar.h>

#include <hdd-ioctl.h>
#include <fileXio_rpc.h>
#include <sys/fcntl.h>

#include <gsKit.h>

#include "main.h"

#ifdef ENABLE_NETWORK_SUPPORT
#include <netman.h>
#include <ps2ip.h>
#endif

#include "hdlfs/hdlfs.h"
#include "HDLGameList.h"

#include "io.h"
#include "system.h"
#include "OSD.h"

#include "graphics.h"
#include "UI.h"
#include "menu.h"
#include "pad.h"

#include "sjis.h"

extern GSGLOBAL *gsGlobal;
extern GSTEXTURE BackgroundTexture;
extern void *_gp;
extern struct RuntimeData RuntimeData;

/*
	The TOC is made up of many records having the structure below.
	There are 3 records in the TOC listed before the first track (With index numbers 0xA0, 0xA1 and 0xA2 in BCD).
	The min field of track 0xA1 records the number of tracks in the disc, excluding tracks 0xA0, 0xA1 and 0xA2.
	The min, sec and frame fields of track 0xA2 records the position of the lead-out area, and hence the total playtime (And hence, number of sectors) of the disc.
*/
/* Source: SCE documentation. */
struct TrackTOCEntry{
	unsigned char AddrCtrl;
	unsigned char TrackNumber;
	unsigned char IndexNumber;
	unsigned char undefined;
	unsigned char undefined2;
	unsigned char undefined3;
	unsigned char zero;
	unsigned char min;
	unsigned char sec;
	unsigned char frame;
};

static int GetNumSectors(sceCdRMode *ReadMode, unsigned char DiscType, unsigned int *sectors, unsigned int *sectorsInLayer1){
	int result;
	unsigned char buffer[2048];
	sceCdlLOCCD TrackLocation;

	*sectors=*sectorsInLayer1=0;

	result=0;
	if((DiscType==SCECdPS2CD)||(DiscType==SCECdPS2CDDA)){
		sceCdGetToc(buffer);
		TrackLocation.minute=((struct TrackTOCEntry *)buffer)[2].min;
		TrackLocation.second=((struct TrackTOCEntry *)buffer)[2].sec;
		TrackLocation.sector=((struct TrackTOCEntry *)buffer)[2].frame;
		TrackLocation.track=((struct TrackTOCEntry *)buffer)[2].TrackNumber;
		*sectors=sceCdPosToInt(&TrackLocation);

		result=((struct TrackTOCEntry *)buffer)[1].min;

		DEBUG_PRINTF("\tTracks: 0x%02x sectors: 0x%0x (%02d:%02d:%02d)\n", result, *sectors, btoi(((struct TrackTOCEntry *)buffer)[2].min), btoi(((struct TrackTOCEntry *)buffer)[2].sec), btoi(((struct TrackTOCEntry *)buffer)[2].frame));
	}
	else if(DiscType==SCECdPS2DVD){	/* PS2DVD-type disc */
		sceCdRead(16, 1, buffer, ReadMode);
		sceCdSync(0);

		/* Copy the number of sectors field. */
		memcpy(sectors, &buffer[80], 4);

		/* DVD9 test. Since the CDVDMAN module in rom0: doesn't support DVD9 detection, attempt to detect the 2nd layer manually. */
		sceCdRead(*sectors, 1, buffer, ReadMode);
		memset(buffer, 0, sizeof(buffer));
		sceCdSync(0);

		DEBUG_PRINTF("\tDVD Layer 0: 0x%08x sectors\n", *sectors);

		if(sceCdGetError()==SCECdErNO){
			if(memcmp(buffer, "CD001", 5)==0){
				memcpy(sectorsInLayer1, &buffer[80], 4);
				DEBUG_PRINTF("\tDVD Layer 1 detected at LSN 0x%08x\n", *sectorsInLayer1);
			}
		}

		result=1;
	}
	else{
		DEBUG_PRINTF("Unrecognized disc type: 0x%02x\n", DiscType);
		result=-EINVAL;
	}

	return result;
}

int GetBootDeviceID(void)
{
	static int BootDevice = -2;
	char path[256];
	int result;

	if(BootDevice < BOOT_DEVICE_UNKNOWN)
	{
		getcwd(path, sizeof(path));

		if(!strncmp(path, "mc0:", 4)) result=BOOT_DEVICE_MC0;
		else if(!strncmp(path, "mc1:", 4)) result=BOOT_DEVICE_MC1;
		else if(!strncmp(path, "cdrom0:", 7)) result=BOOT_DEVICE_CDROM;
		else if(!strncmp(path, "mass:", 5) || !strncmp(path, "mass0:", 6)) result=BOOT_DEVICE_MASS;
		else if(!strncmp(path, "hdd:", 4) || !strncmp(path, "hdd0:", 5)) result=BOOT_DEVICE_HDD;
		else result=BOOT_DEVICE_UNKNOWN;

		BootDevice = result;
	} else
		result = BootDevice;

	return result;
}

static int SysInitMount(void)
{
	static char BlockDevice[38] = "";
	char command[256];
	const char *MountPath;
	int BlockDeviceNameLen, result;

	if(BlockDevice[0] == '\0')
	{
		//Format: hdd0:partition:pfs:/path_to_file_on_partition
		//However, getcwd will return the path, without the filename (as parsed by libc's init.c).
		getcwd(command, sizeof(command));
		if(strlen(command)>6 && (MountPath=strchr(&command[5], ':'))!=NULL)
		{
			BlockDeviceNameLen = (unsigned int)MountPath-(unsigned int)command;
			strncpy(BlockDevice, command, BlockDeviceNameLen);
			BlockDevice[BlockDeviceNameLen]='\0';

			MountPath++;	//This is the location of the mount path;

			if((result = fileXioMount("pfs0:", BlockDevice, FIO_MT_RDONLY)) >= 0)
				result = chdir(MountPath);

			return result;
		} else
			result = -EINVAL;
	} else {
		if((result = fileXioMount("pfs0:", BlockDevice, FIO_MT_RDONLY)) >= 0)
			result = 0;
	}

	return result;
}

int SysBootDeviceInit(void)
{
	int result;

	switch(GetBootDeviceID())
	{
		case BOOT_DEVICE_HDD:
			result = SysInitMount();
			break;
		default:
			result = 0;
	}

	return result;
}

int GetConsoleRegion(void)
{
	static int region = -1;
	FILE *file;

	if(region < 0)
	{
		if((file = fopen("rom0:ROMVER", "r")) != NULL)
		{
			fseek(file, 4, SEEK_SET);
			switch(fgetc(file))
			{
				case 'J':
					region = CONSOLE_REGION_JAPAN;
					break;
				case 'A':
				case 'H':
					region = CONSOLE_REGION_USA;
					break;
				case 'E':
					region = CONSOLE_REGION_EUROPE;
					break;
				case 'C':
					region = CONSOLE_REGION_CHINA;
					break;
			}

			fclose(file);
		}
	}

	return region;
}

int GetConsoleVMode(void)
{
	switch(GetConsoleRegion())
	{
		case CONSOLE_REGION_EUROPE:
			return 1;
		default:
			return 0;
	}
}

/* Generate the partition name in this format: hdd0:PP.SXXX-XXXXX.HDL.<Game Title>*/
void GeneratePartName(char *PartName, const char *DiscID, const wchar_t *title){
	unsigned int i;

	sprintf(PartName, "hdd0:PP.%s.HDL.", DiscID);

	/* Loop through the title, replacing any characters that are neither alphanumeric nor underscore characters with underscore characters, before copying the character to the partition name. */
	for(i=0; i<wcslen(title) && i<14; i++){
		PartName[23+i]=(!iswalnum(title[i]) && title[i]!='_')?'_':title[i];
	}

	PartName[23+i]='\0';
}

static int GetStartupExecName(char *filename){
	char *SystemCNF, *NextLine;
	int fd, result, size;

	if((fd=fileXioOpen("cdrom0:\\SYSTEM.CNF;1", O_RDONLY))>=0){
		size=fileXioLseek(fd, 0, SEEK_END);
		fileXioLseek(fd, 0, SEEK_SET);
		SystemCNF=malloc(size+1);
		fileXioRead(fd, SystemCNF, size);
		fileXioClose(fd);
		SystemCNF[size]='\0';
	}
	else return fd;

	NextLine=strtok(SystemCNF, "\n\r");
	while((NextLine!=NULL) && (strncmp(NextLine, "BOOT2", 5)!=0)){
		NextLine=strtok(NULL, "\n\r");
	}

	free(SystemCNF);

	if(NextLine!=NULL && strcmp(strtok(NextLine, " ="), "BOOT2")==0){
		strncpy(filename, strtok(NULL, " =")+8, 11);	/* Skip the device name part of the path ("cdrom0:\"). */
		filename[11]='\0';
		DEBUG_PRINTF("Startup EXEC path: %s\n", filename);
		result=11;
	}
	else{
		DEBUG_PRINTF("BOOT 2 line not found.\n");
		result=-EINVAL;
	}

	return result;
}

int GetDiscIDFromStartupFilename(const char *StartupFilename, char *DiscID){
	int result;

/*	result=strlen(StartupFilename);
	if((result==11 && StartupFilename[9]!=';' && StartupFilename[10]!='1')	||
		(result==13 && StartupFilename[11]==';' && StartupFilename[12]=='1')){ */
		strncpy(DiscID, StartupFilename, 4);
		DiscID[4]='-';
		strncpy(&DiscID[5], &StartupFilename[5], 3);
		strncpy(&DiscID[8], &StartupFilename[9], 2);
		DiscID[10]='\0';
		result=0;
/*	}
	else{
		DiscID[0]='\0';
		result=-EINVAL;
	} */

	return result;
}

int InitGameCDVDInformation(sceCdRMode *ReadMode, char *DiscID, char *StartupFname, unsigned char *discType, unsigned int *SectorsInDiscLayer0, unsigned int *SectorsInDiscLayer1){
	int result;

	result=0;

	DisplayFlashStatusUpdate(SYS_UI_MSG_READING_DISC);
	DEBUG_PRINTF("\nReading disc...");

	DEBUG_PRINTF("\n\tDisc type: 0x%02x\n", *discType);

	*discType=sceCdGetDiskType();
	if(*discType<SCECdPS2CD || *discType>SCECdPS2DVD){
		DisplayErrorMessage(SYS_UI_MSG_UNSUP_DISC);
		return -EIO;
	}

	sceCdDiskReady(0);

	if((result=GetNumSectors(ReadMode, *discType, SectorsInDiscLayer0, SectorsInDiscLayer1))<0){
		DisplayErrorMessage(SYS_UI_MSG_DISC_READ_ERR);
		return result;
	}

	if((result=GetStartupExecName(StartupFname))<0){
		DEBUG_PRINTF("Error getting executable filename: %d\n", result);
		DisplayErrorMessage(SYS_UI_MSG_SYS_CNF_PARSE_FAIL);
		return result;
	}

	/* Generate the disc ID string in this format: SXXX-YYYZZ. */
	GetDiscIDFromStartupFilename(StartupFname, DiscID);
	DEBUG_PRINTF("Disc ID: %s\n", DiscID);

	return result;
}

static void RetrieveGameInstallationDataBuffer(hdl_game_info *gInfo, const char *partition, struct HDLGameEntry *GameEntry){
	char StartupPath[HDLFS_STARTUP_PTH_LEN+1];
	u32 GameSize;
	int i;

	strncpy(GameEntry->PartName, partition, sizeof(GameEntry->PartName)-1);
	GameEntry->PartName[sizeof(GameEntry->PartName)-1]='\0';
	strncpy(GameEntry->GameTitle, gInfo->gamename, sizeof(GameEntry->GameTitle)-1);
	GameEntry->GameTitle[sizeof(GameEntry->GameTitle)-1]='\0';
	fileXioDevctl("hdl0:", HDLFS_DEVCTL_GET_STARTUP_PATH, NULL, 0, StartupPath, sizeof(StartupPath));
	strncpy(StartupPath, gInfo->startup, HDLFS_STARTUP_PTH_LEN);
	StartupPath[sizeof(StartupPath)-1]='\0';
	GetDiscIDFromStartupFilename(StartupPath, GameEntry->DiscID);
	DEBUG_PRINTF("Disc ID: %s\nPartition: %s\nname: %s\n", GameEntry->DiscID, GameEntry->PartName, GameEntry->GameTitle);
	GameEntry->CompatibilityModeFlags=gInfo->ops2l_compat_flags;
	GameEntry->TRType=gInfo->dma_type;
	GameEntry->TRMode=gInfo->dma_mode;
	GameEntry->DiscType=gInfo->discType;

	GameSize = 0;
	for(i=0; i < gInfo->num_partitions; i++)
		GameSize += gInfo->part_specs[i].part_size / 2048;
	GameEntry->sectors = GameSize;
}

int RetrieveGameInstallationSector(u32 lba, const char *partition, struct HDLGameEntry *GameEntry){
	hdl_game_info *gInfo;
	hddAtaTransfer_t arg;
	int result;

	if ((gInfo = memalign(64, sizeof(hdl_game_info))) != NULL) {
		// Note: The APA specification states that there is a 4KB area used for storing the partition's information, before the extended attribute area.
		arg.lba = lba + (HDL_GAME_DATA_OFFSET+4096)/512;
		arg.size = sizeof(hdl_game_info) / 512;
		if((result = fileXioDevctl("hdd0:", HDIOC_READSECTOR, &arg, sizeof(arg), gInfo, sizeof(hdl_game_info))) == 0){
			RetrieveGameInstallationDataBuffer(gInfo, partition, GameEntry);
		}

		free(gInfo);
	} else
		result = -ENOMEM;

	return result;
}

int RetrieveGameInstallationData(const char *partition, struct HDLGameEntry *GameEntry){
	iox_stat_t stat;
	int result;
	char path[40], StartupPath[61];

	sprintf(path, "hdd0:%s", partition);
	if((result=fileXioMount("hdl0:", path, FIO_MT_RDONLY))>=0){
		if((result=fileXioGetStat("hdl0:", &stat))>=0){
			strncpy(GameEntry->PartName, partition, sizeof(GameEntry->PartName)-1);
			GameEntry->PartName[sizeof(GameEntry->PartName)-1]='\0';
			fileXioDevctl("hdl0:", HDLFS_DEVCTL_GET_TITLE, NULL, 0, GameEntry->GameTitle, sizeof(GameEntry->GameTitle));
			GameEntry->GameTitle[sizeof(GameEntry->GameTitle)-1]='\0';
			fileXioDevctl("hdl0:", HDLFS_DEVCTL_GET_STARTUP_PATH, NULL, 0, StartupPath, sizeof(StartupPath));
			StartupPath[sizeof(StartupPath)-1]='\0';
			GetDiscIDFromStartupFilename(StartupPath, GameEntry->DiscID);
			DEBUG_PRINTF("Disc ID: %s\nPartition: %s\nname: %s\n", GameEntry->DiscID, GameEntry->PartName, GameEntry->GameTitle);
			GameEntry->CompatibilityModeFlags=(unsigned char)(stat.attr>>8);
			GameEntry->TRType=(unsigned char)(stat.attr>>16);
			GameEntry->TRMode=stat.attr>>24;
			GameEntry->DiscType=stat.private_0>>16;
			GameEntry->sectors=stat.size;
		}

		fileXioUmount("hdl0:");
	}

	return result;
}

int UpdateGameInstallation(const char *partition, const wchar_t *title, unsigned char CompatModeFlags, unsigned char TRType, unsigned char TRMode, unsigned char DiscType){
	char path[40], TitleUTF8[HDLFS_GAME_TITLE_LEN];
	iox_stat_t stat;
	int result, StringLen;

	sprintf(path, "hdd0:%s", partition);
	DEBUG_PRINTF("UpdateGameInstallation: Updating %s - %s, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x\n", path, title, CompatModeFlags, TRType, TRMode, DiscType);
	if((result=fileXioMount("hdl0:", path, FIO_MT_RDWR))>=0){
		StringLen=wcstombs(TitleUTF8, title, sizeof(TitleUTF8))+1;
		if((result=fileXioDevctl("hdl0:", HDLFS_DEVCTL_SET_TITLE, TitleUTF8, StringLen, NULL, 0))>=0){
			if((result=fileXioGetStat("hdl0:", &stat))>=0){
				stat.attr=((unsigned int)TRMode)<<24 | ((unsigned int)TRType)<<16 | ((unsigned int)CompatModeFlags)<<8;
				stat.private_0=(stat.private_0&0xFFFF0000)|DiscType<<16;
				if((result=fileXioChStat("hdl0:", &stat, FIO_CST_ATTR|FIO_CST_PRVT))<0){
					DEBUG_PRINTF("UpdateGameInstallation: Can't chstat %s: %d\n", path, result);
				}
			}
			else{
				DEBUG_PRINTF("UpdateGameInstallation: Can't getstat %s: %d\n", path, result);
			}
		}
		else{
			DEBUG_PRINTF("UpdateGameInstallation: Can't rename %s: %d\n", path, result);
		}

		fileXioUmount("hdl0:");
	}
	else{
		DEBUG_PRINTF("UpdateGameInstallation: Can't mount %s: %d\n", path, result);
	}

	return result;
}

int RemoveGameInstallation(const char *PartPath){
	DisplayFlashStatusUpdate(SYS_UI_MSG_PLEASE_WAIT);

	DEBUG_PRINTF("Deleting game at: %s\n", PartPath);
	return fileXioRemove(PartPath);
}

int CheckForExistingGameInstallation(const char *DiscID, char *ExistingPartName, unsigned int BufferSize){
	iox_dirent_t dirent;
	int result, fd;
	char *PartDiscID;

	result=0;
	if((fd=fileXioDopen("hdd0:"))>=0){
		while(fileXioDread(fd, &dirent)>0){
			/* The APA driver stores the partition type in the mode field. */
			if(dirent.stat.mode==HDL_FS_MAGIC && !(dirent.stat.attr&APA_FLAG_SUB)){
				/* Attempt to acquire the game's disc ID. The disc ID portion of the partition's name is after the 1st delimiter. */
				if((PartDiscID=strchr(dirent.name, '.'))!=NULL){
					PartDiscID++;
					if(strncmp(PartDiscID, DiscID, strlen(DiscID))==0){
						result=1;
						if(ExistingPartName!=NULL) strncpy(ExistingPartName, dirent.name, BufferSize);
						break;
					}
				}
			}
		}
	}
	fileXioDclose(fd);

	return result;
}

#define APA_NUMBER_OF_SIZES 6
struct APAPartSizeList{
	unsigned int SizeInSectors;	/* The size of the partition in 2048-byte CD-ROM sectors */
	const char *SizeStr;
};

static const struct APAPartSizeList APAPartSizeList[APA_NUMBER_OF_SIZES]={
	{65536, "128M"},
	{131072, "256M"},
	{262144, "512M"},
	{524288, "1G"},
	{1048576, "2G"},
	{2097152, "4G"}	// The largest PS2 game will only be 8.4GB (DVD9), but it's very wasteful to use a 16GB partition to store such a game. However, the HDLFS uses a 32-bit value to record the size of each slice. Using a partition slice with a user data area of equal to or greater than 4GB will result in an overflow.
};

static int GetPartSize(unsigned long int NumSectors, unsigned int *NumSectorsInPart, const char **SectorTxt, int PartitionNumber, int SizeIndexOverride){
	unsigned int i, ReservedSectors;
	int result, MaxDiscSectorsPerPart;

	/* The maximum partition size is 1/32 the capacity of the disk. */
	MaxDiscSectorsPerPart=fileXioDevctl("hdd0:", HDIOC_MAXSECTOR, NULL, 0, NULL, 0)/4;
	result=-EINVAL;
	ReservedSectors=(PartitionNumber==0)?0x800:1;	/* See comment below. */
	if(SizeIndexOverride>=0){
		if(SizeIndexOverride<APA_NUMBER_OF_SIZES){
			*SectorTxt=(char *)APAPartSizeList[SizeIndexOverride].SizeStr;
			result=SizeIndexOverride;
			*NumSectorsInPart=APAPartSizeList[SizeIndexOverride].SizeInSectors-ReservedSectors;
		}
	}
	else{
		/* If the requested partition size is larger than the largest partition in the list above, limit the requested size.
		(Otherwise, users with really, really large HDDs might get a nice error... assuming that ATAD.IRX allows users to use such a large disk) */
		if(NumSectors>APAPartSizeList[APA_NUMBER_OF_SIZES-1].SizeInSectors-ReservedSectors){
			NumSectors=APAPartSizeList[APA_NUMBER_OF_SIZES-1].SizeInSectors-ReservedSectors;
		}

		for(i=0; i<APA_NUMBER_OF_SIZES; i++){
			if(APAPartSizeList[i].SizeInSectors-ReservedSectors>=NumSectors || APAPartSizeList[i].SizeInSectors-ReservedSectors>=MaxDiscSectorsPerPart-ReservedSectors){
				/*	Judge whether the amount of space remaining is justifiable. If more space can be saved by using several smaller partitions instead of a single large one, do it (e.g, for a 600MB game, use multiple 256MB partitions instead of a single 1GB partition).
					If the amount of space being wasted is equal to or greater than the smallest partition size, use multiple smaller partitions instead of a single large one.	*/
				if(i>0 && APAPartSizeList[i].SizeInSectors-NumSectors>=APAPartSizeList[0].SizeInSectors){
					i--;
				}

				*SectorTxt=(char *)APAPartSizeList[i].SizeStr;
				/* Note: Main partitions have a reserved area of 4MB (0x2000 sectors), while sub-partitions have a reserved area of 2 sectors.
						However, since having a reserved area of 2 sectors means that sub-partitions will have some sectors leftover at the end of them.
						Hence, round up the number of reserved sectors for sub-partitions to 4 to avoid that.
				*/

				result=i;
				*NumSectorsInPart=APAPartSizeList[i].SizeInSectors-ReservedSectors;
				break;
			}
		}
	}

	return result;
}

/* static oid ListExistingPartitions(void){
	printf("dopen:\n");

	iox_dirent_t dirent;
	if((fd=fileXioDopen("hdd0:"))>=0){
		while(fileXioDread(fd, &dirent)>0){
			printf("%s\n", dirent.name);
		}
	}
	fileXioDclose(fd);
} */

static int CreateAndFormatPartition(const char *PartName, const wchar_t *GameTitle, const char *StartupFname, unsigned char CompatibilityModeFlags, unsigned char TRMode, unsigned char TRType, unsigned char discType, unsigned int SectorsInDiscLayer0, unsigned int SectorsInDiscLayer1){
	unsigned int NumSectorsOnDisc, SectorsRemaining;
	const char *PartSizeTxt;
	char PartCreateCmd[32+11+1];
	struct HDLFS_FormatArgs FormatArgs;
	unsigned int PartitionNum, SectorsInPartition;
	int fd, result, PartitionSizeIndex;
	void *FillBuffer;

	result=0;
	SectorsRemaining=NumSectorsOnDisc=SectorsInDiscLayer0+SectorsInDiscLayer1;

	DEBUG_PRINTF(	"HDLGMAN: Create partition:\n"	\
			"\tPartition name:\t\t%s\n"	\
			"\tGame title:\t\t%s\n"	\
			"\tStartup path:\t\t%s\n"	\
			"\tCompatibility flags:\t0x%02x\n"	\
			"\tTransfer type:\t\t0x%02x\n"	\
			"\tTransfer mode:\t\t0x%02x\n"	\
			"\tDisc type:\t\t0x%02x\n"	\
			"\tSectors (Layer 0):\t0x%x\n"	\
			"\tSectors (layer 1):\t0x%x\n", PartName, GameTitle, StartupFname, CompatibilityModeFlags, TRType, TRMode, discType, SectorsInDiscLayer0, SectorsInDiscLayer1);

	/* Create the partition. */
	PartitionSizeIndex=-1;
	do{
		PartitionSizeIndex=GetPartSize(NumSectorsOnDisc, &SectorsInPartition, &PartSizeTxt, 0, PartitionSizeIndex);
		sprintf(PartCreateCmd, "%s,,,%s,HDL", PartName, PartSizeTxt);
		DEBUG_PRINTF("Partition creation cmd: %s\n", PartCreateCmd);
		fd=fileXioOpen(PartCreateCmd, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	}while(fd==-ENOSPC && (--PartitionSizeIndex)>=0);

	/* Proceed, only if the main partition was created successfully. */
	if(fd>=0){
		//Zero-fill the first 512 bytes of the partition attribute area, so the user will know which partition contains the failed installation if this installation does not complete.
		if((FillBuffer=memalign(64, 512))!=NULL){
			memset(FillBuffer, 0, 512);
			fileXioWrite(fd, FillBuffer, 512);
			free(FillBuffer);
		}

		/* Create sub partitions, if there are more sectors than can be stored in the main partition. */
		SectorsRemaining=(SectorsInPartition>SectorsRemaining)?0:SectorsRemaining-SectorsInPartition;
		PartitionNum=1;
		while((SectorsRemaining>0) && (result>=0)){
			PartitionSizeIndex=-1;
			do{
				PartitionSizeIndex=GetPartSize(SectorsRemaining, &SectorsInPartition, &PartSizeTxt, PartitionNum, PartitionSizeIndex);
				DEBUG_PRINTF("Creating sub partition with size: %s\n", PartSizeTxt);
				result=fileXioIoctl2(fd, HDDIO_ADD_SUB, (char *)PartSizeTxt, strlen(PartSizeTxt)+1, NULL, 0);
			}while(result==-ENOSPC && (--PartitionSizeIndex)>=0);

			SectorsRemaining=(SectorsInPartition>SectorsRemaining)?0:SectorsRemaining-SectorsInPartition;
			PartitionNum++;
		}

		fileXioClose(fd);

		if(result>=0){
			memset(&FormatArgs, 0, sizeof(FormatArgs));
			wcstombs(FormatArgs.GameTitle, GameTitle, sizeof(FormatArgs.GameTitle));
			FormatArgs.CompatFlags=CompatibilityModeFlags;
			FormatArgs.DiscType=discType;
			FormatArgs.TRType=TRType;
			FormatArgs.TRMode=TRMode;
			FormatArgs.NumSectors=NumSectorsOnDisc;
			FormatArgs.Layer1Start=(SectorsInDiscLayer1>0)?SectorsInDiscLayer0 - 16:0;
			strncpy(FormatArgs.StartupPath, StartupFname, sizeof(FormatArgs.StartupPath));

			result=fileXioFormat("hdl0:", PartName, &FormatArgs, sizeof(FormatArgs));
			DEBUG_PRINTF("Formatting result: %d\n", result);
		}else{
			printf("Error creating sub-partition. Code: %d\n", result);
		}

		if(result<0){
			fileXioRemove(PartName);
		}
	}
	else{
		printf("Error creating partition with cmd: %s. Code: %d\n", PartCreateCmd, fd);
		result=fd;
	}

	DEBUG_PRINTF("Partition created and formatted. Result: %d\n", result);

	return result;
}

int PrepareInstallHDLGame(const wchar_t *GameTitle, const char *DiscID, const char *StartupFname, const char *TargetPartName, unsigned int SectorsInDiscLayer0, unsigned int SectorsInDiscLayer1, unsigned char DiscType, unsigned char CompatibilityModeFlags, unsigned char TRType, unsigned char TRMode){
	return CreateAndFormatPartition(TargetPartName, GameTitle, StartupFname, CompatibilityModeFlags, TRMode, TRType, DiscType, SectorsInDiscLayer0, SectorsInDiscLayer1);;
}

struct IOBufferData{
	void *DestBuffer;
	unsigned int NumSectors;
};

enum CDVDDriveGameInstallerRemoteCommandValues{
	CDVD_DRIVE_INSTALLER_CMD_NONE=0,
	CDVD_DRIVE_INSTALLER_CMD_ABORT,
};

enum CDVDDriveGameInstallerStatusValues{
	CDVD_DRIVE_INSTALLER_STATE_OK=0,
	CDVD_DRIVE_INSTALLER_STATE_READ_ERROR,
	CDVD_DRIVE_INSTALLER_STATE_WRITE_ERROR,
	CDVD_DRIVE_INSTALLER_STATE_MEM_ERROR,
	CDVD_DRIVE_INSTALLER_STATE_ABORTED
};

struct CDVDDriveGameInstallerParams{
	sceCdRMode *ReadMode;
	int fd;	// The fd value returned from opening the target partition with open().
	volatile char status, command;
	int SyncSemaID;

	unsigned int SectorsInDiscLayer0, SectorsInDiscLayer1;
	volatile float PercentageComplete;
};

static void CDVDDriveGameInstallerThread(struct CDVDDriveGameInstallerParams *params){
	int result;
	unsigned int SectorsRemainingToRead, SectorNum;
	unsigned short int SectorsToRead, SectorsRead;
	struct BuffDesc *bd;
	void *buffers;

	if((result = IOAlloc(&bd, &buffers))>=0){
		if((result = IOWriteInit(params->fd, bd, buffers, IO_BANKSIZE, IO_BANKMAX, CDVD_INSTALL_IO_PRIORITY)) >= 0){
			DEBUG_PRINTF("Now reading sectors.\n");

			SectorsRead = 0;
			result = 0;
			for(SectorsRemainingToRead=params->SectorsInDiscLayer0+params->SectorsInDiscLayer1,SectorNum=0; (result>=0)&&(SectorsRemainingToRead>0); SectorsRemainingToRead-=SectorsToRead,SectorNum+=SectorsToRead){
				SectorsToRead=(SectorsRemainingToRead>IO_BANKSIZE)?IO_BANKSIZE:SectorsRemainingToRead;

				params->PercentageComplete=(float)SectorNum/(params->SectorsInDiscLayer0+params->SectorsInDiscLayer1);

				sceCdSync(0);

				if((result=sceCdGetError())!=SCECdErNO){
					params->status=CDVD_DRIVE_INSTALLER_STATE_READ_ERROR;
					DEBUG_PRINTF("CD/DVD error: %d, sector: 0x%x\n", result, SectorNum);
					result=-EIO;
					break;
				}

				if(SectorsRead > 0){
					//If there were sectors read previously.
					IOSignalWriteDone((unsigned int)SectorsRead * 2048);
					if(IOGetStatus() == IO_THREAD_STATE_ERROR){
						result = -EIO;
						params->status=CDVD_DRIVE_INSTALLER_STATE_WRITE_ERROR;
						break;
					}
				}

				if(params->command!=CDVD_DRIVE_INSTALLER_CMD_NONE){
					switch(params->command){
						case CDVD_DRIVE_INSTALLER_CMD_ABORT:
							DEBUG_PRINTF("Abort command received.\n");
							params->status=CDVD_DRIVE_INSTALLER_STATE_ABORTED;
							break;
						default:
							DEBUG_PRINTF("Unrecognized CD/DVD installer command: %u\n", params->command);
					}

					params->command=CDVD_DRIVE_INSTALLER_CMD_NONE;

					if(params->status!=CDVD_DRIVE_INSTALLER_STATE_OK){
						result=-1;
						break;
					}
				}

				if(sceCdRead(SectorNum, SectorsToRead, IOGetNextWrBuffer(), params->ReadMode)==0){
					params->status=CDVD_DRIVE_INSTALLER_STATE_READ_ERROR;
					DEBUG_PRINTF("sceCdRead fault. Sector: 0x%0x\n", SectorNum);
					result=-EIO;
					break;
				}

				SectorsRead = SectorsToRead;
			}

			if(result>=0){
				sceCdSync(0);

				if((result=sceCdGetError())==SCECdErNO){
					if(SectorsRead > 0){
						IOSignalWriteDone((unsigned int)SectorsRead * 2048);
						if(IOGetStatus() == IO_THREAD_STATE_ERROR){
							result = -EIO;
							params->status=CDVD_DRIVE_INSTALLER_STATE_WRITE_ERROR;
						}
					}

					if((result=IOEndWrite()) < 0){
						params->status=CDVD_DRIVE_INSTALLER_STATE_WRITE_ERROR;
					}
				}
				else{
					IOEndWrite();
					params->status=CDVD_DRIVE_INSTALLER_STATE_READ_ERROR;
					DEBUG_PRINTF("Read fault: %d\n", result);
					result=-EIO;
				}
			}
		}
		else{
			params->status=CDVD_DRIVE_INSTALLER_STATE_MEM_ERROR;
			DEBUG_PRINTF("Error initializing I/O.\n");
		}

		IOFree(&bd, &buffers);
	}
	else{
		params->status=CDVD_DRIVE_INSTALLER_STATE_MEM_ERROR;
		DEBUG_PRINTF("Error allocating memory for I/O.\n");
	}

	SignalSema(params->SyncSemaID);
	ExitDeleteThread();
}

int InstallGameFromCDVDDrive(sceCdRMode *ReadMode, const char *InstallPath, const wchar_t *title, const char *DiscID, const char *StartupFname, unsigned char DiscType, unsigned int SectorsInDiscLayer0, unsigned int SectorsInDiscLayer1, unsigned char CompatibilityModeFlags, unsigned char TRType, unsigned char TRMode){
	int fd, result;
	unsigned int TimeElasped, rate, TotalSizeToTransferKB;
	unsigned char seconds;
	u32 CurrentCPUTicks, PreviousCPUTicks;
	struct CDVDDriveGameInstallerParams params;
	void *CDVDDriveGameInstallerThreadStack;
	ee_sema_t sema;
	unsigned int PadStatus;

	/*	1. Gather information on the game to be installed by parsing SYSTEM.CNF and from the CD/DVD drive or the ISO disc image.
		2. Create the partition, in the HDLoader format. (In other words, a modded version of PS2HDD.irx is required)
		3. Format the partition.
		4. Mount and open the partition.
		5. Copy the disc image to the partition.
		6. Close and unmount the partition.	*/

	DisplayFlashStatusUpdate(SYS_UI_MSG_PLEASE_WAIT);

	if((result=PrepareInstallHDLGame(title, DiscID, StartupFname, InstallPath, SectorsInDiscLayer0, SectorsInDiscLayer1, DiscType, CompatibilityModeFlags, TRType, TRMode))>=0){
		if((result=fileXioMount("hdl0:", InstallPath, FIO_MT_RDWR))>=0){
			if((fd=fileXioOpen("hdl0:", O_WRONLY))>=0){
				DEBUG_PRINTF("Installing game...\n");

				memset(&params, 0, sizeof(params));
				params.ReadMode=ReadMode;
				params.fd=fd;

				sema.init_count=0;
				sema.max_count=1;
				sema.attr=0;
				sema.option=(u32)"disc-install-sync";
				params.SyncSemaID=CreateSema(&sema);
				params.SectorsInDiscLayer0=SectorsInDiscLayer0;
				params.SectorsInDiscLayer1=SectorsInDiscLayer1;
				params.command=CDVD_DRIVE_INSTALLER_CMD_NONE;

				CDVDDriveGameInstallerThreadStack=malloc(0x2000);
				SysCreateThread(&CDVDDriveGameInstallerThread, CDVDDriveGameInstallerThreadStack, 0x2000, &params, CDVD_INSTALL_MAIN_PRIORITY);

				TimeElasped=0;
				PreviousCPUTicks=cpu_ticks();
				while(PollSema(params.SyncSemaID)!=params.SyncSemaID){
					DrawBackground(gsGlobal, &BackgroundTexture);

					PadStatus=ReadCombinedPadStatus();

					if(PadStatus&PAD_CROSS){
						if(DisplayPromptMessage(SYS_UI_MSG_CANCEL_INST, SYS_UI_LBL_CANCEL, SYS_UI_LBL_OK)==2){
							params.command=CDVD_DRIVE_INSTALLER_CMD_ABORT;
						}
					}

					CurrentCPUTicks=cpu_ticks();
					if((seconds=(CurrentCPUTicks>PreviousCPUTicks?CurrentCPUTicks-PreviousCPUTicks:UINT_MAX-PreviousCPUTicks+CurrentCPUTicks)/295000000)>0){
						TimeElasped+=seconds;
						PreviousCPUTicks=CurrentCPUTicks;
					}

					TotalSizeToTransferKB=(SectorsInDiscLayer0+SectorsInDiscLayer1)*(2048/1024);
					rate=(TimeElasped>0)?params.PercentageComplete*TotalSizeToTransferKB/TimeElasped:0;
					DrawInstallGameScreen(title, DiscID, DiscType, params.PercentageComplete, rate, rate>0?((1.0f-params.PercentageComplete)*TotalSizeToTransferKB)/rate:UINT_MAX);

					SyncFlipFB();
				}

				DeleteSema(params.SyncSemaID);
				free(CDVDDriveGameInstallerThreadStack);

				switch(params.status){
					case CDVD_DRIVE_INSTALLER_STATE_READ_ERROR:
						DisplayErrorMessage(SYS_UI_MSG_DISC_READ_ERR);
						result=-EIO;
						break;
					case CDVD_DRIVE_INSTALLER_STATE_WRITE_ERROR:
						DisplayErrorMessage(SYS_UI_MSG_HDD_WRITE_FAULT);
						result=-EIO;
						break;
					case CDVD_DRIVE_INSTALLER_STATE_MEM_ERROR:
						DisplayErrorMessage(SYS_UI_MSG_LACK_OF_MEM);
						result=-ENOMEM;
						break;
					case CDVD_DRIVE_INSTALLER_STATE_ABORTED:
						DisplayInfoMessage(SYS_UI_MSG_INST_CANCELLED);
						result=-1;
						break;
					default:
						result=0;
				}

				fileXioClose(fd);
			}
			else{
				DEBUG_PRINTF("Error opening partition. Result: %d\n", fd);
				DisplayErrorMessage(SYS_UI_MSG_PART_ACC_ERR);
				result=fd;
			}

			fileXioUmount("hdl0:");
		}
		else{
			DEBUG_PRINTF("Error occurred while mounting: %d\n", result);
			DisplayErrorMessage(SYS_UI_MSG_PART_ACC_ERR);
		}
	}
	else{
		DisplayErrorMessage(SYS_UI_MSG_PART_ACC_ERR);
	}

	sceCdStop();

	if(result<0){
		RemoveGameInstallation(InstallPath);
	}

	sceCdSync(0);

	return result;
}

int SysCreateThread(void *function, void *stack, unsigned int StackSize, void *arg, int priority){
	ee_thread_t ThreadData;
	int ThreadID;

	ThreadData.func=function;
	ThreadData.stack=stack;
	ThreadData.stack_size=StackSize;
	ThreadData.gp_reg=&_gp;
	ThreadData.initial_priority=priority;
	ThreadData.attr=ThreadData.option=0;

	if((ThreadID=CreateThread(&ThreadData))>=0){
		if(StartThread(ThreadID, arg)<0){
			DeleteThread(ThreadID);
			ThreadID=-1;
		}
	}

	return ThreadID;
}

enum SPACE_LABEL {
	SPACE_LABEL_MB = 0,
	SPACE_LABEL_GB,

	SPACE_LABEL_COUNT
};

void sysGetFreeDiskSpaceDisplay(char *space)
{
	int result, i;
	u32 free;
	const char *labels[SPACE_LABEL_COUNT] = {
		"MB",
		"GB",
	};

	if((result = fileXioDevctl("hdd0:", HDIOC_FREESECTOR, NULL, 0, &free, sizeof(u32))) != 0)
		free = 0;
	//The disk can only be divided into 128MB parts, at most. Convert from 512-byte sectors into MB.
	free /= (1024*1024/512);

	for(i = 0; i < SPACE_LABEL_COUNT; i++)
	{
		if((free < 1000) || (i + 1 >= SPACE_LABEL_COUNT))
			break;

		free /= 1024;
	}

	sprintf(space, "%lu %s", free, labels[i]);
}

#ifdef ENABLE_NETWORK_SUPPORT
int ethApplyNetIFConfig(int mode)
{
	int result;
	//By default, auto-negotiation (with flow control) is used.
	static int CurrentMode = NETMAN_NETIF_ETH_LINK_MODE_AUTO;

	if(CurrentMode != mode)
	{	//Change the setting, only if different.
		if((result = NetManSetLinkMode(mode)) == 0)
			CurrentMode = mode;
	}else
		result = 0;

	return result;
}

static void EthStatusCheckCb(s32 alarm_id, u16 time, void *common)
{
	iWakeupThread(*(int*)common);
}

static int WaitValidNetState(int (*checkingFunction)(void))
{
	int ThreadID, retry_cycles;

	// Wait for a valid network status;
	ThreadID = GetThreadId();
	for(retry_cycles = 0; checkingFunction() == 0; retry_cycles++)
	{	//Sleep for 1000ms.
		SetAlarm(1000 * 16, &EthStatusCheckCb, &ThreadID);
		SleepThread();

		if(retry_cycles >= 10)	//10s = 10*1000ms
			return -1;
	}

	return 0;
}

static int ethGetDHCPStatus(void)
{
	t_ip_info ip_info;
	int result;

	if ((result = ps2ip_getconfig("sm0", &ip_info)) >= 0)
	{	//Check for a successful state if DHCP is enabled.
		if (ip_info.dhcp_enabled)
			result = (ip_info.dhcp_status == DHCP_STATE_BOUND || (ip_info.dhcp_status == DHCP_STATE_OFF));
		else
			result = -1;
	}

	return result;
}

int ethGetNetIFLinkStatus(void)
{
	return(NetManIoctl(NETMAN_NETIF_IOCTL_GET_LINK_STATUS, NULL, 0, NULL, 0) == NETMAN_NETIF_ETH_LINK_STATE_UP);
}

int ethWaitValidNetIFLinkState(void)
{
	return WaitValidNetState(&ethGetNetIFLinkStatus);
}

int ethWaitValidDHCPState(void)
{
	return WaitValidNetState(&ethGetDHCPStatus);
}

int ethApplyIPConfig(int use_dhcp, const struct ip4_addr *ip, const struct ip4_addr *netmask, const struct ip4_addr *gateway)
{
	t_ip_info ip_info;
	int result;

	//SMAP is registered as the "sm0" device to the TCP/IP stack.
	if ((result = ps2ip_getconfig("sm0", &ip_info)) >= 0)
	{
		//Check if it's the same. Otherwise, apply the new configuration.
		if ((use_dhcp != ip_info.dhcp_enabled)
		    ||	(!use_dhcp &&
			 (!ip_addr_cmp(ip, (struct ip4_addr *)&ip_info.ipaddr) ||
			 !ip_addr_cmp(netmask, (struct ip4_addr *)&ip_info.netmask) ||
			 !ip_addr_cmp(gateway, (struct ip4_addr *)&ip_info.gw))))
		{
			if (use_dhcp)
			{
				ip_info.dhcp_enabled = 1;
			}
			else
			{	//Copy over new settings if DHCP is not used.
				ip_addr_set((struct ip4_addr *)&ip_info.ipaddr, ip);
				ip_addr_set((struct ip4_addr *)&ip_info.netmask, netmask);
				ip_addr_set((struct ip4_addr *)&ip_info.gw, gateway);

				ip_info.dhcp_enabled = 0;
			}

			//Update settings.
			result = ps2ip_setconfig(&ip_info);
		}
		else
			result = 0;
	}

	return result;
}

//This will apply link & network settings. It will return without waiting, hence ethValidate() should be used to validate that the network is accessible.
void ethInit(void)
{
	struct ip4_addr IP, NM, GW;
	int mode;

	if(RuntimeData.UseDHCP)
	{
		ip4_addr_set_zero(&IP);
		ip4_addr_set_zero(&NM);
		ip4_addr_set_zero(&GW);
	} else {
		IP4_ADDR(&IP, RuntimeData.ip_address[0], RuntimeData.ip_address[1], RuntimeData.ip_address[2], RuntimeData.ip_address[3]);
		IP4_ADDR(&NM, RuntimeData.subnet_mask[0], RuntimeData.subnet_mask[1], RuntimeData.subnet_mask[2], RuntimeData.subnet_mask[3]);
		IP4_ADDR(&GW, RuntimeData.gateway[0], RuntimeData.gateway[1], RuntimeData.gateway[2], RuntimeData.gateway[3]);
	}

	if(!RuntimeData.AdvancedNetworkSettings)
	{
		mode = NETMAN_NETIF_ETH_LINK_MODE_AUTO;
	} else {
		mode = RuntimeData.EthernetLinkMode;
		if(!RuntimeData.EthernetFlowControl)
			mode |= NETMAN_NETIF_ETH_LINK_DISABLE_PAUSE;
	}

	ethApplyNetIFConfig(mode);	//If it cannot be done, don't fail here.

	ps2ipInit(&IP, &NM, &GW);

	if(RuntimeData.UseDHCP)
	{	//Enable DHCP (static IP is used by default).
		ethApplyIPConfig(1, &IP, &NM, &GW);
	}
}

//This will apply the new link & network settings. It will return without waiting, hence ethValidate() should be used to validate that the network is accessible.
void ethReinit(void)
{
	int result, mode;
	struct ip4_addr IP, NM, GW;

	if(RuntimeData.UseDHCP)
	{
		ip4_addr_set_zero(&IP);
		ip4_addr_set_zero(&NM);
		ip4_addr_set_zero(&GW);
	} else {
		IP4_ADDR(&IP, RuntimeData.ip_address[0], RuntimeData.ip_address[1], RuntimeData.ip_address[2], RuntimeData.ip_address[3]);
		IP4_ADDR(&NM, RuntimeData.subnet_mask[0], RuntimeData.subnet_mask[1], RuntimeData.subnet_mask[2], RuntimeData.subnet_mask[3]);
		IP4_ADDR(&GW, RuntimeData.gateway[0], RuntimeData.gateway[1], RuntimeData.gateway[2], RuntimeData.gateway[3]);
	}

	if(!RuntimeData.AdvancedNetworkSettings)
	{
		mode = NETMAN_NETIF_ETH_LINK_MODE_AUTO;
	} else {
		mode = RuntimeData.EthernetLinkMode;
		if(!RuntimeData.EthernetFlowControl)
			mode |= NETMAN_NETIF_ETH_LINK_DISABLE_PAUSE;
	}

	//Attempt to apply the new link setting.
	if(ethApplyNetIFConfig(mode) != 0)
	{
		DisplayErrorMessage(SYS_UI_MSG_NO_NETWORK_CONNECTION);
	}

	ethApplyIPConfig(RuntimeData.UseDHCP, &IP, &NM, &GW);
}

//Validates that the network is accessible, given the current settings.
void ethValidate(void)
{
	if(ethWaitValidNetIFLinkState() == 0)
	{
		if(RuntimeData.UseDHCP)
		{
			if (ethWaitValidDHCPState() != 0)
				DisplayErrorMessage(SYS_UI_MSG_DHCP_ERROR);
		}
	} else
		DisplayErrorMessage(SYS_UI_MSG_NO_NETWORK_CONNECTION);
}

void ethGetIPAddressDisplay(char *ip)
{
	t_ip_info ip_info;
	int result;
	u8 ip_address[4];

	//SMAP is registered as the "sm0" device to the TCP/IP stack.
	if ((result = ps2ip_getconfig("sm0", &ip_info)) >= 0)
	{
		ip_address[0] = ip4_addr1((struct ip4_addr *)&ip_info.ipaddr);
		ip_address[1] = ip4_addr2((struct ip4_addr *)&ip_info.ipaddr);
		ip_address[2] = ip4_addr3((struct ip4_addr *)&ip_info.ipaddr);
		ip_address[3] = ip4_addr4((struct ip4_addr *)&ip_info.ipaddr);
		sprintf(ip, "%u.%u.%u.%u", ip_address[0], ip_address[1], ip_address[2], ip_address[3]);
	} else
		ip[0] = '\0';
}
#endif
