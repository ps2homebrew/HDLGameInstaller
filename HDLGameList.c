#include <errno.h>
#include <kernel.h>
#include <libcdvd.h>
#include <libpad.h>
#include <loadfile.h>
#include <malloc.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <fileXio_rpc.h>
#include <sys/fcntl.h>

#include "main.h"
#ifdef ENABLE_NETWORK_SUPPORT
#include <ps2ip.h>
#include <netman.h>
#endif

#include "HDLGameList.h"
#include "system.h"
#include "hdlfs/hdlfs.h"

struct HDLGameLinkedListNode{
	struct HDLGameLinkedListNode *next;
	struct HDLGameEntry HDLGameEntry;
};

static int LoadHDLGameListIntoBuffer(struct HDLGameEntry **GameList);
static void FreeHDLGameListInBuffer(struct HDLGameEntry *GameList);
static unsigned int CurrentHDLGameListGeneration=0;

static struct HDLGameEntry *CentralGameList=NULL;
static int NumGamesInCentralGameList=0;
static int CentralGameListLockSemaID;

extern struct RuntimeData RuntimeData;

void LockCentralHDLGameList(void){
	WaitSema(CentralGameListLockSemaID);
}

void UnlockCentralHDLGameList(void){
	SignalSema(CentralGameListLockSemaID);
}

void InitCentralHDLGameList(void){
	ee_sema_t sema;
	sema.init_count=1;
	sema.max_count=1;
	sema.attr=0;
	sema.option=(u32)"GameListLock";
	CentralGameListLockSemaID=CreateSema(&sema);
}

void DeinitCentralHDLGameList(void){
	WaitSema(CentralGameListLockSemaID);
	DeleteSema(CentralGameListLockSemaID);
	FreeHDLGameList();
}

static int GameEntryComparator(const void *e1, const void *e2)
{
	return(strcmp(((struct HDLGameEntry*)e1)->GameTitle, ((struct HDLGameEntry*)e2)->GameTitle));
}

int LoadHDLGameList(struct HDLGameEntry **GameList){
	LockCentralHDLGameList();

	FreeHDLGameListInBuffer(CentralGameList);
	NumGamesInCentralGameList=LoadHDLGameListIntoBuffer(&CentralGameList);
	if(RuntimeData.SortTitles)
		qsort(CentralGameList, NumGamesInCentralGameList, sizeof(struct HDLGameEntry), &GameEntryComparator);

	CurrentHDLGameListGeneration++;

	UnlockCentralHDLGameList();

	if(GameList!=NULL) *GameList=CentralGameList;

	return NumGamesInCentralGameList;
}

unsigned int GetHDLGameListGeneration(void){
	return CurrentHDLGameListGeneration;
}

int GetHDLGameList(struct HDLGameEntry **GameList){
	if(GameList!=NULL) *GameList=CentralGameList;
	return NumGamesInCentralGameList;
}

void FreeHDLGameList(void){
	FreeHDLGameListInBuffer(CentralGameList);
	NumGamesInCentralGameList=0;
	CentralGameList = NULL;
}

static int LoadHDLGameListIntoBuffer(struct HDLGameEntry **GameList){
	struct HDLGameLinkedListNode *LinkedList, *first, *prev;
	struct HDLGameEntry GameEntry;
	iox_dirent_t dirent;
	int result, fd;
	unsigned int NumGames, i;

	DEBUG_PRINTF("Loading game list...\n");

	NumGames=0;
	first=NULL;
	LinkedList=NULL;
	*GameList=NULL;
	result=0;
	if((fd=fileXioDopen("hdd0:"))>=0){
		while(fileXioDread(fd, &dirent)>0){
			/* The APA driver stores the partition type in the mode field. */
			if(dirent.stat.mode==HDL_FS_MAGIC && !(dirent.stat.attr&APA_FLAG_SUB)){
				if(RetrieveGameInstallationSector(dirent.stat.private_5, dirent.name, &GameEntry)==0){
					/* The head of the list needs to be formed first. */
					if(first==NULL) {
						LinkedList=malloc(sizeof(struct HDLGameLinkedListNode));
						first=LinkedList;
					} else {
						LinkedList->next=malloc(sizeof(struct HDLGameLinkedListNode));
						LinkedList=LinkedList->next;
					}

					NumGames++;
					memcpy(&LinkedList->HDLGameEntry, &GameEntry, sizeof(LinkedList->HDLGameEntry));
					LinkedList->next=NULL;
				}
			}
		}
		fileXioDclose(fd);
	}

	/* Now, if no errors occurred, begin consolidating the game list. */
	if(result>=0 && NumGames>0){
		*GameList=malloc(sizeof(struct HDLGameEntry)*NumGames);
		LinkedList=first;
		for(i=0; LinkedList!=NULL; i++){
			memcpy(&(*GameList)[i], &LinkedList->HDLGameEntry, sizeof(struct HDLGameEntry));

			prev=LinkedList;
			LinkedList=LinkedList->next;
			free(prev);
		}

		result=NumGames;
	}

	DEBUG_PRINTF("Game list loaded. result: %d\n", result);

	return(result<0?0:result);
}

static void FreeHDLGameListInBuffer(struct HDLGameEntry *GameList){
	if(GameList!=NULL) free(GameList);
}

