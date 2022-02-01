#include <errno.h>
#include <stdio.h>
#include <winsock2.h>

#include "HDLGManClient.h"
#include "client.h"
#include "main.h"
#include "OSD.h"

static int HandlePacket(SOCKET ClientSocket, unsigned char *buffer);

int HDLGManPrepareGameInstall(const wchar_t *GameTitle, const char *DiscID, const char *StartupFname, unsigned char DiscType, u32 SectorsInDiscLayer0, u32 SectorsInDiscLayer1, unsigned char CompatibilityModeFlags, unsigned char TRType, unsigned char TRMode)
{
	struct HDLGameInfo GameInfo;
	int result, s;

	if((s = PrepareDataConnection()) >= 0)
	{
		WideCharToMultiByte(CP_UTF8, 0, GameTitle, -1, GameInfo.GameTitle, sizeof(GameInfo.GameTitle), NULL, NULL);
		GameInfo.GameTitle[sizeof(GameInfo.GameTitle)-1]='\0';
		strncpy(GameInfo.DiscID, DiscID, sizeof(GameInfo.DiscID)-1);
		GameInfo.DiscID[sizeof(GameInfo.DiscID)-1]='\0';
		strncpy(GameInfo.StartupFname, StartupFname, sizeof(GameInfo.StartupFname)-1);
		GameInfo.StartupFname[sizeof(GameInfo.StartupFname)-1]='\0';
		GameInfo.DiscType=DiscType;
		GameInfo.SectorsInDiscLayer0=SectorsInDiscLayer0;
		GameInfo.SectorsInDiscLayer1=SectorsInDiscLayer1;
		GameInfo.CompatibilityFlags=CompatibilityModeFlags;
		GameInfo.TRType=TRType;
		GameInfo.TRMode=TRMode;
		if((result=SendCmdPacket(&GameInfo, HDLGMAN_SERVER_PREP_GAME_INST, sizeof(struct HDLGameInfo)))>=0)
			result=GetResponse(NULL, 0);

		if(result >= 0)
			result = AcceptDataConnection(s);
		else
			ClosePendingDataConnection(s);
	} else
		result = s;

	return result;
}

int HDLGManWriteGame(const void *buffer, unsigned int NumBytes)
{
	int result;

	result = SendData(buffer, NumBytes);

	return(result == NumBytes ? 0 : -EEXTCONNLOST);
}

int HDLGManInitGameWrite(const char *partition, u32 sectors, u32 offset)
{
	int result, s;
	struct IOInitReq req;

	if((s = PrepareDataConnection()) >= 0)
	{
		strcpy(req.partition, partition);
		req.sectors = sectors;
		req.offset = offset;
		if((result=SendCmdPacket(&req, HDLGMAN_SERVER_INIT_GAME_WRITE, sizeof(req)))>=0)
			result=GetResponse(NULL, 0);

		if(result >= 0)
			result = AcceptDataConnection(s);
		else
			ClosePendingDataConnection(s);
	} else
		result = s;

	return result;
}

int HDLGManInitGameRead(const char *partition, u32 sectors, u32 offset)
{
	int result, s;
	struct IOInitReq req;

	if((s = PrepareDataConnection()) >= 0)
	{
		strcpy(req.partition, partition);
		req.sectors = sectors;
		req.offset = offset;
		if((result=SendCmdPacket(&req, HDLGMAN_SERVER_INIT_GAME_READ, sizeof(req)))>=0)
			result=GetResponse(NULL, 0);

		if(result >= 0)
			result = AcceptDataConnection(s);
		else
			ClosePendingDataConnection(s);
	} else
		result = s;

	return result;
}

int HDLGManReadGame(void *buffer, unsigned int NumBytes)
{
	int result;

	result = RecvData(buffer, NumBytes);

	return(result == NumBytes ? 0 : -EEXTCONNLOST);
}

int HDLGManGetIOStatus(void)
{
	int result;

	if((result=SendCmdPacket(NULL, HDLGMAN_SERVER_IO_STATUS, 0))>=0)
		result=GetResponse(NULL, 0);

	return result;
}

int HDLGManCloseGame(void)
{
	int result;

	if((result=SendCmdPacket(NULL, HDLGMAN_SERVER_CLOSE_GAME, 0))>=0)
		result=GetResponse(NULL, 0);

	return result;
}

int HDLGManLoadGameList(struct HDLGameEntry **GameList)
{
	int NumGamesInList, i, result;
	struct HDLGameEntryTransit GameEntryTransit;
	struct HDLGameEntry *GameEntry;

	if((result=SendCmdPacket(NULL, HDLGMAN_SERVER_LOAD_GAME_LIST, 0))>=0)
	{
		if((NumGamesInList=GetResponse(NULL, 0))>0)
		{
			if((*GameList=malloc(sizeof(struct HDLGameEntry)*NumGamesInList))!=NULL)
			{
				if(((result=SendCmdPacket(NULL, HDLGMAN_SERVER_READ_GAME_LIST, 0))<0) || ((result=GetResponse(NULL, 0))<0))
				{
					NumGamesInList=0;
					free(*GameList);
				}
				else
				{
					result = 0;
					for(i = 0; i < NumGamesInList; i++)
					{
						if((result = GetPayload(&GameEntryTransit, sizeof(struct HDLGameEntryTransit))) == sizeof(struct HDLGameEntryTransit))
						{
							GameEntry=&(*GameList)[i];
							MultiByteToWideChar(CP_UTF8, 0, GameEntryTransit.GameTitle, -1, GameEntry->GameTitle, GAME_TITLE_MAX_LEN+1);
							GameEntry->CompatibilityModeFlags=GameEntryTransit.CompatibilityModeFlags;
							strcpy(GameEntry->DiscID, GameEntryTransit.DiscID);
							GameEntry->DiscType=GameEntryTransit.DiscType;
							strcpy(GameEntry->PartName, GameEntryTransit.PartName);
							GameEntry->TRMode=GameEntryTransit.TRMode;
							GameEntry->TRType=GameEntryTransit.TRType;
							GameEntry->sectors=GameEntryTransit.sectors;
						} else {
							break;
						}
					}
				}
			}
			else NumGamesInList=0;
		}
	/*	else if(NumGamesInList<0){
			printf("HDLGMan (debug): Can't load game list: %d\n", NumGamesInList);
		} */
	}
	else
	{
		//printf("HDLGMan (debug): Can't send LOAD_GAME_LIST command: %d\n", result);
		NumGamesInList=0;
	}

	return NumGamesInList;
}

int HDLGManLoadGameListEntry(struct HDLGameEntry *GameListEntry, int index)
{
	int NumGamesInList, result;
	struct HDLGameEntryTransit GameEntryTransit;

	if((result=SendCmdPacket(NULL, HDLGMAN_SERVER_LOAD_GAME_LIST, 0))>=0)
	{
		if((NumGamesInList=GetResponse(NULL, 0))>0)
		{
				if(((result=SendCmdPacket(&index, HDLGMAN_SERVER_READ_GAME_LIST_ENTRY, sizeof(index)))<0) || ((result=GetResponse(&GameEntryTransit, sizeof(struct HDLGameEntryTransit)))<0))
				{
					return result;
				}
				else
				{
					MultiByteToWideChar(CP_UTF8, 0, GameEntryTransit.GameTitle, -1, GameListEntry->GameTitle, GAME_TITLE_MAX_LEN+1);
					GameListEntry->CompatibilityModeFlags=GameEntryTransit.CompatibilityModeFlags;
					strcpy(GameListEntry->DiscID, GameEntryTransit.DiscID);
					GameListEntry->DiscType=GameEntryTransit.DiscType;
					strcpy(GameListEntry->PartName, GameEntryTransit.PartName);
					GameListEntry->TRMode=GameEntryTransit.TRMode;
					GameListEntry->TRType=GameEntryTransit.TRType;
					GameListEntry->sectors=GameEntryTransit.sectors;
				}
		}
	/*	else if(NumGamesInList<0){
			printf("HDLGMan (debug): Can't load game list: %d\n", NumGamesInList);
		} */
	}
	else
	{
		//printf("HDLGMan (debug): Can't send LOAD_GAME_LIST command: %d\n", result);
		NumGamesInList=0;
	}

	return NumGamesInList;
}

int HDLGManReadGameEntry(const char *partition, struct HDLGameEntry *GameEntry)
{
	int result;
	struct HDLGameEntryTransit GameEntryTransit;

	if((result=SendCmdPacket(partition, HDLGMAN_SERVER_READ_GAME_ENTRY, strlen(partition)+1))>=0)
	{
		if((result=GetResponse(&GameEntryTransit, sizeof(struct HDLGameEntryTransit)))==0)
		{
			MultiByteToWideChar(CP_UTF8, 0, GameEntryTransit.GameTitle, -1, GameEntry->GameTitle, GAME_TITLE_MAX_LEN+1);
			GameEntry->CompatibilityModeFlags=GameEntryTransit.CompatibilityModeFlags;
			strcpy(GameEntry->DiscID, GameEntryTransit.DiscID);
			GameEntry->DiscType=GameEntryTransit.DiscType;
			strcpy(GameEntry->PartName, GameEntryTransit.PartName);
			GameEntry->TRMode=GameEntryTransit.TRMode;
			GameEntry->TRType=GameEntryTransit.TRType;
			GameEntry->sectors=GameEntryTransit.sectors;
		}
	}

	return result;
}

int HDLGManUpdateGameEntry(struct HDLGameEntry *GameEntry)
{
	struct HDLGameEntryTransit GameEntryTransit;
	int result;

	WideCharToMultiByte(CP_UTF8, 0, GameEntry->GameTitle, -1, GameEntryTransit.GameTitle, sizeof(GameEntryTransit.GameTitle), NULL, NULL);
	GameEntryTransit.CompatibilityModeFlags=GameEntry->CompatibilityModeFlags;
	strcpy(GameEntryTransit.DiscID, GameEntry->DiscID);
	GameEntryTransit.DiscType=GameEntry->DiscType;
	strcpy(GameEntryTransit.PartName, GameEntry->PartName);
	GameEntryTransit.TRMode=GameEntry->TRMode;
	GameEntryTransit.TRType=GameEntry->TRType;

	if((result=SendCmdPacket(&GameEntryTransit, HDLGMAN_SERVER_UPD_GAME_ENTRY, sizeof(struct HDLGameEntryTransit)))>=0)
		result=GetResponse(NULL, 0);

	return result;
}

int HDLGManDeleteGameEntry(const char *partition)
{
	int result;

	if((result=SendCmdPacket(partition, HDLGMAN_SERVER_DEL_GAME_ENTRY, strlen(partition)+1))>=0)
		result=GetResponse(NULL, 0);

	return result;
}

int HDLGManGetGamePartName(const char *DiscID, char *partition)
{
	int result;

	if((result=SendCmdPacket(DiscID, HDLGMAN_SERVER_GET_GAME_PART_NAME, strlen(DiscID)+1))>=0)
		result=GetResponse(partition, 33);

	return result;
}

unsigned long int HDLGManGetFreeSpace(void)
{
	int result;
	u32 space;

	if((result=SendCmdPacket(NULL, HDLGMAN_SERVER_GET_FREE_SPACE, 0))>=0)
		result = GetResponse(&space, sizeof(u32));

	return(result != 0 ? 0 : space);
}

int HDLGManInitOSDDefaultResources(const char *partition)
{
	int result;

	if((result=SendCmdPacket(partition, HDLGMAN_SERVER_INIT_DEFAULT_OSD_RESOURCES, strlen(partition)+1))>=0)
		result=GetResponse(NULL, 0);

	return result;
}

int HDLGManInitOSDResources(const char *partition, const char *DiscID, const wchar_t *OSDTitleLine1, const wchar_t *OSDTitleLine2, int UseSaveData)
{
	struct OSDResourceInitReq InitReq;
	int result;

	strcpy(InitReq.partition, partition);
	strcpy(InitReq.DiscID, DiscID);
	WideCharToMultiByte(CP_UTF8, 0, OSDTitleLine1, -1, InitReq.OSDTitleLine1, sizeof(InitReq.OSDTitleLine1), NULL, NULL);
	InitReq.OSDTitleLine1[OSD_TITLE_MAX_LEN_BYTES]='\0';
	WideCharToMultiByte(CP_UTF8, 0, OSDTitleLine2, -1, InitReq.OSDTitleLine2, sizeof(InitReq.OSDTitleLine2), NULL, NULL);
	InitReq.OSDTitleLine2[OSD_TITLE_MAX_LEN_BYTES]='\0';
	InitReq.UseSaveData=UseSaveData;
	if((result=SendCmdPacket(&InitReq, HDLGMAN_SERVER_INIT_OSD_RESOURCES, sizeof(struct OSDResourceInitReq)))>=0)
		result=GetResponse(NULL, 0);

	return result;
}

int HDLGManOSDResourceLoad(int index, const void *buffer, unsigned int length)
{
	int result;
	struct OSDResourceWriteReq WriteReq;

	WriteReq.index=index;
	WriteReq.length=length;
	if((result=SendCmdPacket(&WriteReq, HDLGMAN_SERVER_OSD_RES_LOAD_INIT, sizeof(struct OSDResourceWriteReq)))>=0)
	{
		if((result=GetResponse(NULL, 0))==0)
		{
			if((result=SendCmdPacket(buffer, HDLGMAN_SERVER_OSD_RES_LOAD, length))>=0)
				result=GetResponse(NULL, 0);
		}
	}

	return result;
}

int HDLGManWriteOSDResources(void)
{
	int result;

	if((result=SendCmdPacket(NULL, HDLGMAN_SERVER_WRITE_OSD_RESOURCES, 0))>=0)
		result=GetResponse(NULL, 0);

	return result;
}

int HDLGManOSDResourceWriteCancel(void)
{
	int result;

	if((result=SendCmdPacket(NULL, HDLGMAN_SERVER_OSD_RES_WRITE_CANCEL, 0))>=0)
		result=GetResponse(NULL, 0);

	return result;
}

int HDLGManGetOSDResourcesStat(const char *partition, struct OSDResourceStat *stat)
{
	struct OSDResourceStatReq StatReq;
	int result;

	strcpy(StatReq.partition, partition);
	if((result=SendCmdPacket(&StatReq, HDLGMAN_SERVER_GET_OSD_RES_STAT, sizeof(struct OSDResourceStatReq)))>=0)
		result=GetResponse(stat, sizeof(struct OSDResourceStat));

	return result;
}

int HDLGManReadOSDResourceFile(const char *partition, int index, void *buffer, unsigned int length)
{
	struct OSDResourceReadReq ReadReq;
	int result;

	strcpy(ReadReq.partition, partition);
	ReadReq.index=index;
	if((result=SendCmdPacket(&ReadReq, HDLGMAN_SERVER_OSD_RES_READ, sizeof(struct OSDResourceReadReq)))>=0)
		result=GetResponse(buffer, length);

	return result;
}

int HDLGManCheckExistingMCSaveData(const char *DiscID)
{
	struct OSD_MC_ResourceStatReq MC_StatReq;
	int result;

	strcpy(MC_StatReq.DiscID, DiscID);
	if((result=SendCmdPacket(&MC_StatReq, HDLGMAN_SERVER_OSD_MC_SAVE_CHECK, sizeof(struct OSD_MC_ResourceStatReq)))>=0)
		result=GetResponse(NULL, 0);

	return result;
}

int HDLGManGetMCSaveDataFileStat(const char *DiscID, const char *filename)
{
	struct OSD_MC_ResourceReq MC_ResStatReq;
	int result;

	strcpy(MC_ResStatReq.DiscID, DiscID);
	strncpy(MC_ResStatReq.filename, filename, sizeof(MC_ResStatReq.filename));
	if((result=SendCmdPacket(&MC_ResStatReq, HDLGMAN_SERVER_OSD_MC_GET_RES_STAT, sizeof(struct OSD_MC_ResourceReq)))>=0)
		result=GetResponse(NULL, 0);

	return result;
}

int HDLGManReadMCSaveDataFile(const char *DiscID, const char *filename, void *buffer, unsigned int length)
{
	struct OSD_MC_ResourceReadReq MC_ResReadReq;
	int result;

	strcpy(MC_ResReadReq.DiscID, DiscID);
	strncpy(MC_ResReadReq.filename, filename, sizeof(MC_ResReadReq.filename));
	MC_ResReadReq.length=length;
	if((result=SendCmdPacket(&MC_ResReadReq, HDLGMAN_SERVER_OSD_MC_RES_READ, sizeof(struct OSD_MC_ResourceReadReq)))>=0)
		result=GetResponse(buffer, length);

	return result;
}

int HDLGManPowerOffServer(void)
{
	return SendCmdPacket(NULL, HDLGMAN_SERVER_SHUTDOWN, 0)>=0?0:-EIO;
}

int HDLGManGetGameInstallationOSDTitles(const char *partition, struct OSD_Titles *titles)
{
	struct OSD_TitlesTransit OSDTitlesTransit;
	int result;

	if((result=SendCmdPacket(partition, HDLGMAN_SERVER_OSD_RES_READ_TITLES, strlen(partition)+1))>=0)
	{
		if((result=GetResponse(&OSDTitlesTransit, sizeof(struct OSD_TitlesTransit)))==0)
		{
			MultiByteToWideChar(CP_UTF8, 0, OSDTitlesTransit.title1, -1, titles->title1, OSD_TITLE_MAX_LEN+1);
			MultiByteToWideChar(CP_UTF8, 0, OSDTitlesTransit.title2, -1, titles->title2, OSD_TITLE_MAX_LEN+1);
		}
	}

	return result;
}
