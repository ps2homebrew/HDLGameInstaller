#include <kernel.h>
#include <libcdvd.h>
#include <limits.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <libmc.h>
#include <libpwroff.h>
#include <loadfile.h>
#include <malloc.h>
#include <sifcmd.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>
#include <ps2ip.h>
#include <wchar.h>

#include <speedregs.h>
#include <atahw.h>

#include <debug.h>
#include <libhdd.h>
#include <fileXio_rpc.h>
#include <hdd-ioctl.h>
#include <sys/fcntl.h>

#include "main.h"
#include "HDLGameList.h"
#include "system.h"
#include "io.h"
#include "OSD.h"
#include "HDLGameSvr.h"

typedef int SOCKET;

extern void *_gp;

enum TASK_MODES{
	TASK_GAME_INSTALLATION=0,
	TASK_OSD_RESOURCE_CONFIG,
	TASK_NONE=-1
};

struct GameInstallationContext{
	int fd;
	int write;
	u32 remaining;
	char partition[40];
	struct BuffDesc *bd;
	void *ioBuffer;
};

struct OSDResourceConfigContext{
	char partition[40];
	struct OSDResourceFileEntry resources[NUM_OSD_FILES_ENTS];
	void *NewFileBuffer;	//Temporary pointer for storing the address of the buffer of an incoming resource file.
	unsigned int AllocatedBufferLength, NewFileResourceIndex;
};

enum THREAD_CMD {
	THREAD_CMD_NOP = 0,
	THREAD_CMD_STOP,
};

struct ClientData{
	u8 RxBuffer[HDLGMAN_RECV_MAX] ALIGNED(64);
	u8 TxBuffer[HDLGMAN_SEND_MAX] ALIGNED(64);
	u8 stack[CLIENT_THREAD_STACK_SIZE] ALIGNED(16);
	u8 DataThreadStack[CLIENT_DATA_THREAD_STACK_SIZE] ALIGNED(16);
	struct sockaddr_in peer;
	SOCKET socket;
	SOCKET DataSocket;
	int task;
	void *TaskData;
	unsigned char status;
	unsigned char DataThreadRunning;
	unsigned char ServerThreadRunning;
	int ServerThreadID;
	int DataThreadID;
	int StateSemaID;	//Semaphore for locking the thread running state.
};

struct ServerData{
	int cmd;
	unsigned char status;
	int CmdThreadID;	//Thread that is waiting for the command to complete.
	int CmdSemaID;		//Only 1 thread can issue a command.
	int StateSemaID;	//Semaphore for locking the thread running state.
};

static SOCKET srvSocket;
static struct ClientData ClientData[MAX_CLIENTS] ALIGNED(64);
static u8 NumClientsConnected;
static u8 NeedReloadGameList;
struct ServerData ServerData;

extern struct RuntimeData RuntimeData;

static void ServerThreadExecCmd(int cmd);
static int SendResponse(SOCKET connection, int rcode, const void *payload, unsigned int PayloadLength);
static int SendPayload(SOCKET connection, const void *payload, unsigned int PayloadLength);
static int ReceiveData(SOCKET connection, void *buffer, int length);
static int GetResponse(SOCKET connection, void *buffer, int NumBytes);
static int CreateServerThread(void);
static int CreateClientThread(struct ClientData *client);
static void DeinitServer(void);
static void DisconnectSocketConnection(SOCKET connection);
static void DisconnectDataConnection(SOCKET connection);
static int EndInstallation(struct ClientData *client);
static int CloseGame(struct ClientData *client);
static int CleanupClientConnection(struct ClientData *client);
static int PowerOffServer(struct ClientData *client, void *buffer, unsigned int length);
static int HandlePacket(struct ClientData *client, unsigned int command, unsigned char *buffer, unsigned int length);
static void ServerClientService(struct ClientData *client);
static int StartClientThread(struct ClientData *client, SOCKET s, const struct sockaddr *peer);
static void ServerLoop(void);
static void DataThread(struct ClientData *client);
static int MountOpenGame(struct GameInstallationContext *InstallationContext, struct ClientData *client, u32 sectors, u32 offset, int write);
static int PrepareGameInstallation(struct ClientData *client, void *buffer, unsigned int length);
static int InitDataConnection(struct ClientData *client);
static int InitGameWrite(struct ClientData *client, void *buffer, unsigned int length);
static int WriteGame(struct ClientData *client);
static int InitGameRead(struct ClientData *client, void *buffer, unsigned int length);
static int ReadGame(struct ClientData *client);
static int HandleCloseGame(struct ClientData *client);
static int GetIOStatus(struct ClientData *client);
static int LoadGameList(struct ClientData *client, void *buffer, unsigned int length);
static int ReadGameList(struct ClientData *client, void *buffer, unsigned int length);
static int ReadGameListEntry(struct ClientData *client, void *buffer, unsigned int length);
static int ReadGameListEntryDetails(struct ClientData *client, void *buffer, unsigned int length);
static int UpdateGameListEntry(struct ClientData *client, void *buffer, unsigned int length);
static int DeleteGameEntry(struct ClientData *client, void *buffer, unsigned int length);
static int GetPartName(struct ClientData *client, void *buffer, unsigned int length);
static int GetFreeSpace(struct ClientData *client);
static int InitDefaultOSDResources(struct ClientData *client, void *buffer, unsigned int length);
static int InitOSDResources(struct ClientData *client, void *buffer, unsigned int length);
static int OSDResourceLoadInit(struct ClientData *client, void *buffer, unsigned int length);
static int WriteOSDResources(struct ClientData *client, void *buffer, unsigned int length);
static int CancelOSDResourceWrite(struct ClientData *client, void *buffer, unsigned int length);
static int GetOSDResourceStat(struct ClientData *client, void *buffer, unsigned int length);
static int ReadOSDResource(struct ClientData *client, void *buffer, unsigned int length);
static int ReadOSDResourceTitles(struct ClientData *client, void *buffer, unsigned int length);
static int CheckMCSave(struct ClientData *client, void *buffer, unsigned int length);
static int GetMCSaveResourceStat(struct ClientData *client, void *buffer, unsigned int length);
static int ReadMCSaveResource(struct ClientData *client, void *buffer, unsigned int length);
static int PowerOffServer(struct ClientData *client, void *buffer, unsigned int length);
static int HandlePacket(struct ClientData *client, unsigned int command, unsigned char *buffer, unsigned int length);

static void ServerThreadExecCmd(int cmd)
{
	int valid;

	WaitSema(ServerData.CmdSemaID);

	WaitSema(ServerData.StateSemaID);
	valid = (ServerData.status & SERVER_STATUS_THREAD_RUN) != 0;
	SignalSema(ServerData.StateSemaID);

	if(valid)
	{
		ServerData.CmdThreadID = GetThreadId();
		ServerData.cmd = cmd;
		SleepThread();
	}

	SignalSema(ServerData.CmdSemaID);
}

static int SendResponse(SOCKET connection, int rcode, const void *payload, unsigned int PayloadLength)
{
	const unsigned char *ptr;
	unsigned int remaining;
	struct HDDToolsPacketHdr header;
	int result;

	header.command=HDLGMAN_SERVER_RESPONSE;
	header.result=rcode;
	header.PayloadLength=PayloadLength;

	for(ptr=(const unsigned char*)&header,remaining=sizeof(struct HDDToolsPacketHdr); remaining>0; remaining-=result,ptr+=result)
	{
		if((result=send(connection, ptr, remaining, 0))<=0)
		{
			result=-1;
			break;
		}
	}

	if(result>=0)
		result = SendPayload(connection, payload, PayloadLength);

	return result;
}

static int SendPayload(SOCKET connection, const void *payload, unsigned int PayloadLength)
{
	const unsigned char *ptr;
	unsigned int remaining;
	struct HDDToolsPacketHdr header;
	int result;

	remaining = PayloadLength;
	ptr = payload;
	result = 0;
	while(remaining>0)
	{
		if((result=send(connection, ptr, remaining, 0))>0)
		{
			ptr+=result;
			remaining-=result;
		}
		else{
			result=-1;
			break;
		}
	}

	return(result > 0 ? PayloadLength : result);
}

static int ReceiveData(SOCKET connection, void *buffer, int length)
{
	struct timeval timeout;
	int result, remaining;
	char *ptr;
	fd_set readFDs;

	for (ptr = (char*)buffer, remaining = length, result = 0; remaining > 0; remaining -= result, ptr += result)
	{
		timeout.tv_sec = 30;
		timeout.tv_usec = 0;
		FD_ZERO(&readFDs);
		FD_SET(connection, &readFDs);
		if ((result = select(connection + 1, &readFDs, NULL, NULL, &timeout)) == 1)
		{
			if ((result = recv(connection, ptr, remaining, 0)) <= 0)
			{
				result = -EEXTCONNLOST;
				break;
			}
		}
		else {
			result = -EEXTCONNLOST;
			DisconnectSocketConnection(connection);
			break;
		}
	}

	if (result > 0) result = length;

	return result;
}

static int GetResponse(SOCKET connection, void *buffer, int NumBytes)
{
	int result;
	struct HDDToolsPacketHdr header;

	//Receive header.
	if ((result = ReceiveData(connection, (void*)&header, sizeof(header))) == sizeof(header))
	{
		//Receive payload.
		if (NumBytes > 0 && header.PayloadLength > 0)
		{
			if ((result = ReceiveData(connection, buffer, (int)header.PayloadLength > NumBytes ? NumBytes : header.PayloadLength)) > 0)
				result = header.result;
		}
		else
			result = header.result;
	}

	return result;
}

static int MainServerThreadID;
static unsigned char ServerThreadStack[SERVER_THREAD_STACK_SIZE] __attribute__((aligned(16)));

static int CreateServerThread(void)
{
	ee_sema_t sema;

	ServerData.status = SERVER_STATUS_THREAD_RUN;
	ServerData.CmdThreadID = -1;
	ServerData.cmd = THREAD_CMD_NOP;

	sema.init_count = 1;
	sema.max_count = 1;
	sema.attr = 0;
	sema.option = (u32)"server-cmd";
	if((ServerData.CmdSemaID = CreateSema(&sema)) >= 0)
	{
		sema.init_count = 1;
		sema.max_count = 1;
		sema.attr = 0;
		sema.option = (u32)"server-state";
		if((ServerData.StateSemaID = CreateSema(&sema)) >= 0)
		{
			MainServerThreadID=SysCreateThread(&ServerLoop, ServerThreadStack, SERVER_THREAD_STACK_SIZE, NULL, SERVER_MAIN_THREAD_PRIORITY);
			if(MainServerThreadID < 0)
			{
				DeleteSema(ServerData.StateSemaID);
				DeleteSema(ServerData.CmdSemaID);
			}

			return MainServerThreadID;
		} else {
			DeleteSema(ServerData.CmdSemaID);
			return ServerData.StateSemaID;
		}
	} else {
		return ServerData.CmdSemaID;
	}
}

static int CreateClientThread(struct ClientData *client)
{
	ee_sema_t sema;
	ee_thread_t thread;
	int ThreadID;

	sema.init_count = 1;
	sema.max_count = 1;
	sema.attr = 0;
	sema.option = (u32)"client-state";
	if((client->StateSemaID = CreateSema(&sema)) >= 0)
	{
		thread.func = (void*)&DataThread;
		thread.stack = client->DataThreadStack;
		thread.stack_size = CLIENT_THREAD_STACK_SIZE;
		thread.gp_reg = &_gp;
		thread.initial_priority = SERVER_CLIENT_THREAD_PRIORITY;
		thread.attr = 0;
		thread.option = 0;

		//Create the data thread. It will be in DORMANT state.
		if((client->DataThreadID = CreateThread(&thread)) >= 0)
		{
			thread.func = (void*)ServerClientService;
			thread.stack = client->stack;
			thread.stack_size = CLIENT_THREAD_STACK_SIZE;
			thread.gp_reg = &_gp;
			thread.initial_priority = SERVER_CLIENT_THREAD_PRIORITY;
			thread.attr = 0;
			thread.option = 0;

			//Create the client server thread. It will be in DORMANT state.
			if((client->ServerThreadID = CreateThread(&thread)) >= 0)
				return 0;

			DeleteThread(client->DataThreadID);
			DeleteSema(client->StateSemaID);
			return client->ServerThreadID;
		}
		else
		{
			DeleteSema(client->StateSemaID);
			return client->DataThreadID;
		}
	} else {
		return client->StateSemaID;
	}
}

int InitializeStartServer(void)
{
	ee_sema_t sema;
	struct sockaddr_in addr;
	unsigned int i;
	int result;

	DI();
	RuntimeData.IsRemoteClientConnected = 0;
	NumClientsConnected = 0;
	NeedReloadGameList = 0;
	EI();

	srvSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVER_PORT_NUM);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if(bind(srvSocket, (struct sockaddr*)&addr, sizeof(addr))==0 && listen(srvSocket, MAX_CLIENTS)==0)
	{
		for(i=0; i<MAX_CLIENTS; i++)
		{
			ClientData[i].status = 0;
			ClientData[i].ServerThreadID = -1;
			ClientData[i].task = TASK_NONE;
			ClientData[i].TaskData = NULL;
			ClientData[i].ServerThreadRunning = 0;
			ClientData[i].DataThreadRunning = 0;
			if ((result = CreateClientThread(&ClientData[i])) != 0)
			{
				printf("Failed to create client thread: %d\n", result);
				DisconnectSocketConnection(srvSocket);
				return result;
			}
		}

		result=0;
	}
	else
	{
		result = -1;
		DisconnectSocketConnection(srvSocket);
	}

	if(result>=0)
		CreateServerThread();

	return result;
}

void DeinitializeServer(void)
{
	ServerThreadExecCmd(THREAD_CMD_STOP);
	TerminateThread(MainServerThreadID);
	DeleteThread(MainServerThreadID);
	MainServerThreadID=-1;

	DeleteSema(ServerData.CmdSemaID);
	ServerData.CmdSemaID = -1;
	DeleteSema(ServerData.StateSemaID);
	ServerData.StateSemaID = -1;
}

static void DeinitServer(void)
{
	unsigned int i;
	struct ClientData *client;
	ee_thread_status_t info;

	for(i=0; i<MAX_CLIENTS; i++)
	{
		if(ClientData[i].ServerThreadID >= 0)
		{
			client = &ClientData[i];

			if(!client->ServerThreadRunning)
			{
				DeleteThread(client->ServerThreadID);
				DeleteThread(client->DataThreadID);
				client->ServerThreadID = -1;
				client->DataThreadID = -1;

				DeleteSema(client->StateSemaID);
				client->StateSemaID = -1;
			} else {
				printf("Server: thread %d continued until shutdown.\n", client->ServerThreadID);
			}
		}
	}
}

static void DisconnectSocketConnection(SOCKET connection)
{
	shutdown(connection, SHUT_RDWR);
	closesocket(connection);
}

//Close the socket gracefully, waiting for the remote to close the connection. Use this to complete successful data transfers.
static void DisconnectDataConnection(SOCKET connection)
{
	char dummy;

	shutdown(connection, SHUT_WR);
	//Wait for the remote end to close the socket, which shall happen after all sent data has been received.
	while(recv(connection, &dummy, sizeof(dummy), 0) > 0);
	closesocket(connection);
}

static int EndInstallation(struct ClientData *client)
{
	int result;
	struct GameInstallationContext *InstallationContext;

	/* Allow this to succeed by default. If the client and server's states do not match up,
	   allowing the client to close the game will allow the user to start a new operation. */
	result = 0;
	if (client->task == TASK_GAME_INSTALLATION)
	{
		InstallationContext = client->TaskData;

		if(InstallationContext->write)
			result = IOEndWrite();
		else
			result = IOEndRead();

		IOFree(&InstallationContext->bd, &InstallationContext->ioBuffer);

		fileXioClose(InstallationContext->fd);
		fileXioUmount("hdl0:");
		client->status&=~(CLIENT_STATUS_OPENED_FILE|CLIENT_STATUS_MOUNTED_FS);
		client->task=TASK_NONE;
		free(client->TaskData);
		client->TaskData=NULL;
		SignalSema(RuntimeData.InstallationLockSema);
	}

	return result;
}

static int CloseGame(struct ClientData *client)
{
	int result;

	WaitSema(client->StateSemaID);

	result = EndInstallation(client);

	SignalSema(client->StateSemaID);

	return result;
}

static int CleanupClientConnection(struct ClientData *client)
{
	int reload;

	WaitSema(client->StateSemaID);

	switch(client->task)
	{
		case TASK_GAME_INSTALLATION:
			EndInstallation(client);
			break;
	}

	client->status = 0;
	client->task = TASK_NONE;
	if(client->TaskData != NULL)
	{
		free(client->TaskData);
		client->TaskData = NULL;
	}

	SignalSema(client->StateSemaID);

	DI();
	--NumClientsConnected;
	if(NumClientsConnected < 1)
		RuntimeData.IsRemoteClientConnected = 0;

	if(NeedReloadGameList)
	{	//Reload the game list
		NeedReloadGameList = 0;
		reload = 1;
	} else {
		reload = 0;
	}
	EI();

	if(reload)
		LoadHDLGameList(NULL);

	return 0;
}

static void ServerClientService(struct ClientData *client)
{
	struct timeval timeout;
	fd_set readFDs;
	int result, SkipPayload, id;
	struct HDDToolsPacketHdr header;

	client->ServerThreadRunning = 1;

	printf("Client connected.\n");

	while(1)
	{
		result = recv(client->socket, &header, sizeof(header), 0);

		if(result > 0)
		{
			if(result != sizeof(struct HDDToolsPacketHdr))
			{
				DEBUG_PRINTF("Invalid header length: %d\n", result);
				break;
			}

			switch(header.command)
			{
				//Exceptions for automatic payload reading
				default:
					SkipPayload = 0;
			}

			if((!SkipPayload)
			&& (header.PayloadLength > 0))
			{
				if(header.PayloadLength>sizeof(client->RxBuffer))
				{
					printf("Error: Payload too long.\n");
					if((result=SendResponse(client->socket, -ENOMEM, NULL, 0))<0)
					{
						DEBUG_PRINTF("Response send error: %d\n", result);
						break;
					}
					continue;
				}

				if((result=ReceiveData(client->socket, client->RxBuffer, header.PayloadLength)) != header.PayloadLength)
				{
					if(result > 0)
						result = -1;
					DEBUG_PRINTF("Error retrieving payload: %d\n", result);
					break;
				}
			}

			if((result=HandlePacket(client, header.command, client->RxBuffer, header.PayloadLength))<0)
			{
				DEBUG_PRINTF("Error handling command: %d.\n", result);
				break;
			}
		} else {
			break;
		}
	}

	printf("Client disconnected.\n");

	DisconnectSocketConnection(client->socket);
	CleanupClientConnection(client);

	client->ServerThreadRunning = 0;
}

static int StartClientThread(struct ClientData *client, SOCKET s, const struct sockaddr *peer)
{
	client->socket = s;
	client->status = CLIENT_STATUS_CONNECTED;
	memcpy(&client->peer, peer, sizeof(struct sockaddr));

	return StartThread(client->ServerThreadID, client);
}

static void ServerLoop(void)
{
	unsigned int i;
	unsigned char done;
	SOCKET TempSocketConnection;
	int size, id;
	struct sockaddr peer;
	struct timeval timeout;
	fd_set readFDs;

	printf("Server started.\n");

	done = 0;
	while(!done)
	{
		timeout.tv_sec = 3;
		timeout.tv_usec = 0;
		FD_ZERO(&readFDs);
		FD_SET(srvSocket, &readFDs);
		if (select(srvSocket + 1, &readFDs, NULL, NULL, &timeout) == 1)
		{
			size=sizeof(peer);
			if((TempSocketConnection=accept(srvSocket, &peer, &size))>=0)
			{
				for(i=0; i<MAX_CLIENTS; i++)
				{
					if(ClientData[i].status == 0)
					{	/* Find an empty slot. */
						if(StartClientThread(&ClientData[i], TempSocketConnection, &peer) >= 0)
						{
							DI();
							++NumClientsConnected;
							RuntimeData.IsRemoteClientConnected = 1;
							EI();
						}
						break;
					}
				}
				if(i==MAX_CLIENTS)
				{
					DisconnectSocketConnection(TempSocketConnection);
					DEBUG_PRINTF("Error! Out of connection slots.\n");
				}
			} else {
				done = 1;
			}
		} else {
			switch(ServerData.cmd) {
				case THREAD_CMD_STOP:
					done = 1;
					break;
				default:
					ServerData.cmd = THREAD_CMD_NOP;
			}
		}
	}

	printf("Server shutting down.");

	DisconnectSocketConnection(srvSocket);
	DeinitServer();

	WaitSema(ServerData.StateSemaID);
	ServerData.status &= ~SERVER_STATUS_THREAD_RUN;

	if(ServerData.CmdThreadID >= 0)
	{
		id = ServerData.CmdThreadID;
		ServerData.CmdThreadID = -1;
		WakeupThread(id);
	}

	SignalSema(ServerData.StateSemaID);	//Unlock state
}

static void DataThread(struct ClientData *client)
{
	int result;
	struct GameInstallationContext *InstallationContext;

	client->DataThreadRunning = 1;

	if(client->task == TASK_GAME_INSTALLATION)
	{
		InstallationContext=client->TaskData;

		if(InstallationContext->write)
			result = WriteGame(client);
		else
			result = ReadGame(client);

		//Close the socket gracefully.
		DisconnectDataConnection(client->DataSocket);

		//Close the game here, regardless of how it went.
		CloseGame(client);
	}

	client->DataThreadRunning = 0;
}

static int MountOpenGame(struct GameInstallationContext *InstallationContext, struct ClientData *client, u32 sectors, u32 offset, int write)
{
	int result;

	InstallationContext->write = write;

	if((result=fileXioMount("hdl0:", InstallationContext->partition, write ? FIO_MT_RDWR : FIO_MT_RDONLY))>=0)
	{
		if((result=InstallationContext->fd=fileXioOpen("hdl0:", write? O_RDWR : O_RDONLY))>=0)
		{
			if(offset != 0)
				result = fileXioLseek(InstallationContext->fd, (s32)offset, SEEK_SET) == (s32)offset ? 0 : -ESPIPE;

			if(result >= 0)
			{
				InstallationContext->remaining = sectors;

				if((result = IOAlloc(&InstallationContext->bd, &InstallationContext->ioBuffer)) >= 0)
				{
					if(write)
						result = IOWriteInit(InstallationContext->fd, InstallationContext->bd, InstallationContext->ioBuffer, IO_BANKSIZE, IO_BANKMAX, SERVER_IO_THREAD_PRIORITY);
					else
						result = IOReadInit(InstallationContext->fd, InstallationContext->bd, InstallationContext->ioBuffer, IO_BANKSIZE, IO_BANKMAX, sectors, SERVER_IO_THREAD_PRIORITY);

					if(result < 0)
					{
						DEBUG_PRINTF("Error occurred while initializing I/O: %d\n", result);
						IOFree(&InstallationContext->bd, &InstallationContext->ioBuffer);
					}
				}
				else
					DEBUG_PRINTF("Could not allocate memory for I/O.\n");
			}

			if(result < 0)
				fileXioClose(InstallationContext->fd);
		} else
			DEBUG_PRINTF("Error opening partition. Result: %d\n", InstallationContext->fd);
		if(result < 0)
			fileXioUmount("hdl0:");
	}
	else
		DEBUG_PRINTF("Error occurred while mounting: %d\n", result);

	return result;
}

static int PrepareGameInstallation(struct ClientData *client, void *buffer, unsigned int length)
{
	int result;
	struct GameInstallationContext *InstallationContext;
	const struct HDLGameInfo* info = (const struct HDLGameInfo*)buffer;
	wchar_t GameTitle[GAME_TITLE_MAX_LEN+1];

	WaitSema(client->StateSemaID);

	if((client->task==TASK_NONE) && (PollSema(RuntimeData.InstallationLockSema)==RuntimeData.InstallationLockSema))
	{
		if((client->TaskData=malloc(sizeof(struct GameInstallationContext)))!=NULL)
		{
			InstallationContext=client->TaskData;
			InstallationContext->write = 1;

			mbstowcs(GameTitle, info->GameTitle, GAME_TITLE_MAX_LEN+1);
			GeneratePartName(InstallationContext->partition, info->DiscID, GameTitle);
			result=PrepareInstallHDLGame(GameTitle, info->DiscID, info->StartupFname, InstallationContext->partition,
				info->SectorsInDiscLayer0, info->SectorsInDiscLayer1, info->DiscType, info->CompatibilityFlags,
				info->TRType, info->TRMode);
			if(result>=0)
			{
				if((result = MountOpenGame(InstallationContext, client, info->SectorsInDiscLayer0+info->SectorsInDiscLayer1, 0, 1)) >= 0)
				{
					client->status |= (CLIENT_STATUS_MOUNTED_FS|CLIENT_STATUS_OPENED_FILE);
					client->task=TASK_GAME_INSTALLATION;

					DI();
					NeedReloadGameList = 1;
					EI();

					result = InitDataConnection(client);
				}
				else
					RemoveGameInstallation(InstallationContext->partition);
			}

			if(result<0)
				EndInstallation(client);
		}
		else result=-ENOMEM;
	}
	else result=-EBUSY;

	SignalSema(client->StateSemaID);

	return SendResponse(client->socket, result, NULL, 0);
}

static int InitDataConnection(struct ClientData *client)
{
	struct sockaddr_in addr;
	SOCKET s;
	int result;

	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVER_DATA_PORT_NUM);
	addr.sin_addr.s_addr = client->peer.sin_addr.s_addr;
	if((result = connect(s, (struct sockaddr*)&addr, sizeof(addr))) == 0)
	{
		if(StartThread(client->DataThreadID, client) == client->DataThreadID)
		{
			client->DataSocket = s;
			result = 0;
		} else
			result = -1;
	} else {
		result = -1;
	}

	if(result != 0)
		DisconnectSocketConnection(s);

	return result;
}

static int InitGameWrite(struct ClientData *client, void *buffer, unsigned int length)
{
	int result;
	struct GameInstallationContext *InstallationContext;
	const struct IOInitReq *req = (const struct IOInitReq*)buffer;

	WaitSema(client->StateSemaID);

	if(client->task==TASK_NONE && (PollSema(RuntimeData.InstallationLockSema)==RuntimeData.InstallationLockSema))
	{
		if((client->TaskData=malloc(sizeof(struct GameInstallationContext)))!=NULL)
		{
			InstallationContext=client->TaskData;
			snprintf(InstallationContext->partition, sizeof(InstallationContext->partition), "hdd0:%s", req->partition);

			if((result = MountOpenGame(InstallationContext, client, req->sectors, req->offset, 1)) >= 0)
			{
				client->status |= (CLIENT_STATUS_MOUNTED_FS|CLIENT_STATUS_OPENED_FILE);
				client->task=TASK_GAME_INSTALLATION;

				result = InitDataConnection(client);
			}

			if(result<0)
				EndInstallation(client);
		}
		else result=-ENOMEM;
	}
	else result=-EBUSY;

	SignalSema(client->StateSemaID);

	return SendResponse(client->socket, result, NULL, 0);
}

//Receive the pre-specified amount of sectors or until remote closes the connection.
static int WriteGame(struct ClientData *client)
{
	int result, len, lenBytes;
	void *wrBuffer;
	struct GameInstallationContext *InstallationContext = client->TaskData;

	result = 0;
	while((InstallationContext->remaining > 0) && (result == 0))
	{
		wrBuffer = IOGetNextWrBuffer();

		len = InstallationContext->remaining > IO_BANKSIZE ? IO_BANKSIZE : InstallationContext->remaining;
		lenBytes = len * 2048;
		if(ReceiveData(client->DataSocket, wrBuffer, lenBytes) == lenBytes)
		{
			IOSignalWriteDone(lenBytes);

			if(IOGetStatus() == IO_THREAD_STATE_ERROR)
			{
				result = -EIO;
				break;
			}

			InstallationContext->remaining -= len;
			result = 0;
		} else {
			result = -EPIPE;
			break;
		}
	}

	return result;
}

static int InitGameRead(struct ClientData *client, void *buffer, unsigned int length)
{
	int result;
	struct GameInstallationContext *InstallationContext;
	const struct IOInitReq *req = (const struct IOInitReq*)buffer;

	WaitSema(client->StateSemaID);

	if(client->task==TASK_NONE && (PollSema(RuntimeData.InstallationLockSema)==RuntimeData.InstallationLockSema))
	{
		if((client->TaskData=malloc(sizeof(struct GameInstallationContext)))!=NULL)
		{
			InstallationContext=client->TaskData;
			InstallationContext->write = 0;
			snprintf(InstallationContext->partition, sizeof(InstallationContext->partition), "hdd0:%s", req->partition);

			if((result = MountOpenGame(InstallationContext, client, req->sectors, req->offset, 0)) >= 0)
			{
				client->status |= (CLIENT_STATUS_MOUNTED_FS|CLIENT_STATUS_OPENED_FILE);
				client->task=TASK_GAME_INSTALLATION;

				result = InitDataConnection(client);
			}

			if(result<0)
				EndInstallation(client);
		}
		else result=-ENOMEM;
	}
	else result=-EBUSY;

	SignalSema(client->StateSemaID);

	return SendResponse(client->socket, result, NULL, 0);
}

static int ReadGame(struct ClientData *client)
{
	int result, len;
	struct GameInstallationContext *InstallationContext=client->TaskData;
	const void *RespPayload;
	const u8 *ptr;
	unsigned int RespPayloadLength, remaining;

	result = 0;
	while((InstallationContext->remaining > 0) && (result == 0))
	{
		if((result = IOReadNext(&RespPayload)) >= 0)
		{
			RespPayloadLength = result;
			result = 0;
			for(ptr=(const u8*)RespPayload, remaining=RespPayloadLength; remaining>0; remaining-=len,ptr+=len)
			{
				if((len = send(client->DataSocket, ptr, remaining, 0))<=0)
				{
					result=-EPIPE;
					break;
				}
			}

			if(result == 0)
			{
				if(IOGetStatus() == IO_THREAD_STATE_ERROR)
				{
					result = -EIO;
					break;
				}

				InstallationContext->remaining -= RespPayloadLength / 2048;

				if(InstallationContext->remaining > 0)
					IOReadAdvance();

				result = 0;
			}
		}
	}

	return result;
}

static void CloseGameThreadCB(s32 alarm_id, u16 time, void *arg)
{
	iWakeupThread((int)arg);
}

static int HandleCloseGame(struct ClientData *client)
{
	int result;
	struct GameInstallationContext *InstallationContext;

	//Wait for the data thread to terminate.
	while(client->DataThreadRunning)
	{
		printf("CloseGame: waiting for data thread %d.\n", client->DataThreadID);
		SetAlarm(1000 * 16, &CloseGameThreadCB, (void*)GetThreadId());
		SleepThread();
	}

	result = 0;	//Game was already closed by the data thread.
	return SendResponse(client->socket, result, NULL, 0);
}

static int GetIOStatus(struct ClientData *client)
{
	int result;

	result = IOGetStatus();
	return SendResponse(client->socket, result, NULL, 0);
}

static int LoadGameList(struct ClientData *client, void *buffer, unsigned int length)
{
	int reload, NumGames;

	DI();
	if(NeedReloadGameList)
	{	//Reload the game list
		NeedReloadGameList = 0;
		reload = 1;
	} else {
		reload = 0;
	}
	EI();

	NumGames = (reload) ? LoadHDLGameList(NULL) : GetHDLGameList(NULL);

	return SendResponse(client->socket, NumGames, NULL, 0);
}

static int ReadGameList(struct ClientData *client, void *buffer, unsigned int length)
{
	int NumGamesInList, result, i, rcode;
	struct HDLGameEntry *GameList, *pGameEntry;
	struct HDLGameEntryTransit GameEntryTransit;

	LockCentralHDLGameList();

	if((NumGamesInList = GetHDLGameList(&GameList)) > 0)
	{
		if((result = SendResponse(client->socket, 0, NULL, 0)) == 0)
		{
			for(i = 0; i < NumGamesInList; i++)
			{
				pGameEntry=&GameList[i];

				strcpy(GameEntryTransit.GameTitle, pGameEntry->GameTitle);
				GameEntryTransit.CompatibilityModeFlags=pGameEntry->CompatibilityModeFlags;
				strcpy(GameEntryTransit.DiscID, pGameEntry->DiscID);
				GameEntryTransit.DiscType=pGameEntry->DiscType;
				strcpy(GameEntryTransit.PartName, pGameEntry->PartName);
				GameEntryTransit.TRMode=pGameEntry->TRMode;
				GameEntryTransit.TRType=pGameEntry->TRType;
				GameEntryTransit.sectors=pGameEntry->sectors;

				if((result = SendPayload(client->socket, &GameEntryTransit, sizeof(struct HDLGameEntryTransit))) != sizeof(struct HDLGameEntryTransit))
					break;
			}
		}
	}
	else
	{
		result = SendResponse(client->socket, -ENOENT, NULL, 0);
	}

	UnlockCentralHDLGameList();

	return result;
}

static int ReadGameListEntry(struct ClientData *client, void *buffer, unsigned int length)
{
	int NumGamesInList, result;
	struct HDLGameEntry *GameList, *pGameEntry;
	struct HDLGameEntryTransit GameEntryTransit;
	void *RespPayload;
	unsigned int RespPayloadLength;

	LockCentralHDLGameList();

	RespPayload=NULL;
	RespPayloadLength=0;
	if((NumGamesInList=GetHDLGameList(&GameList))>0)
	{
		pGameEntry=&GameList[*(int*)buffer];

		strcpy(GameEntryTransit.GameTitle, pGameEntry->GameTitle);
		GameEntryTransit.CompatibilityModeFlags=pGameEntry->CompatibilityModeFlags;
		strcpy(GameEntryTransit.DiscID, pGameEntry->DiscID);
		GameEntryTransit.DiscType=pGameEntry->DiscType;
		strcpy(GameEntryTransit.PartName, pGameEntry->PartName);
		GameEntryTransit.TRMode=pGameEntry->TRMode;
		GameEntryTransit.TRType=pGameEntry->TRType;
		GameEntryTransit.sectors=pGameEntry->sectors;
		RespPayload=&GameEntryTransit;
		RespPayloadLength=sizeof(struct HDLGameEntryTransit);
		result=0;
	}
	else result=-ENOENT;

	UnlockCentralHDLGameList();

	return SendResponse(client->socket, result, RespPayload, RespPayloadLength);
}

static int ReadGameListEntryDetails(struct ClientData *client, void *buffer, unsigned int length)
{
	int result;
	struct HDLGameEntry GameEntry;
	struct HDLGameEntryTransit GameEntryTransit;
	void *RespPayload;
	unsigned int RespPayloadLength;

	RespPayload=NULL;
	RespPayloadLength=0;
	if((result=RetrieveGameInstallationData(buffer, &GameEntry))>=0)
	{
		strcpy(GameEntryTransit.GameTitle, GameEntry.GameTitle);
		GameEntryTransit.CompatibilityModeFlags=GameEntry.CompatibilityModeFlags;
		strcpy(GameEntryTransit.DiscID, GameEntry.DiscID);
		GameEntryTransit.DiscType=GameEntry.DiscType;
		strcpy(GameEntryTransit.PartName, GameEntry.PartName);
		GameEntryTransit.TRMode=GameEntry.TRMode;
		GameEntryTransit.TRType=GameEntry.TRType;
		GameEntryTransit.sectors=GameEntry.sectors;
		RespPayload=&GameEntryTransit;
		RespPayloadLength=sizeof(struct HDLGameEntryTransit);
	}

	return SendResponse(client->socket, result, RespPayload, RespPayloadLength);
}

static int UpdateGameListEntry(struct ClientData *client, void *buffer, unsigned int length)
{
	int result;
	wchar_t GameTitle[GAME_TITLE_MAX_LEN+1];

	mbstowcs(GameTitle, ((struct HDLGameEntryTransit*)buffer)->GameTitle, GAME_TITLE_MAX_LEN);
	GameTitle[GAME_TITLE_MAX_LEN] = '\0';
	result=UpdateGameInstallation(((struct HDLGameEntryTransit*)buffer)->PartName, GameTitle, ((struct HDLGameEntryTransit*)buffer)->CompatibilityModeFlags, ((struct HDLGameEntryTransit*)buffer)->TRType, ((struct HDLGameEntryTransit*)buffer)->TRMode, ((struct HDLGameEntryTransit*)buffer)->DiscType);

	if (result >= 0)
		NeedReloadGameList = 1;

	return SendResponse(client->socket, result, NULL, 0);
}

static int DeleteGameEntry(struct ClientData *client, void *buffer, unsigned int length)
{
	char PartName[40];
	int result;

	snprintf(PartName, sizeof(PartName), "hdd0:%s", buffer);
	result = RemoveGameInstallation(PartName);

	if (result >= 0)
		NeedReloadGameList = 1;

	return SendResponse(client->socket, result, NULL, 0);
}

static int GetPartName(struct ClientData *client, void *buffer, unsigned int length)
{
	int result;
	void *RespPayload;
	unsigned int RespPayloadLength;
	char PartName[40];

	RespPayload=NULL;
	RespPayloadLength=0;
	if(CheckForExistingGameInstallation(buffer, PartName, sizeof(PartName)))
	{
		RespPayload=PartName;
		RespPayloadLength=strlen(PartName)+1;
		result=0;
	}
	else result=-ENOENT;

	return SendResponse(client->socket, result, RespPayload, RespPayloadLength);
}

static int GetFreeSpace(struct ClientData *client)
{
	int result;

	*(u32*)client->TxBuffer = 0;
	result = fileXioDevctl("hdd0:", HDIOC_FREESECTOR, NULL, 0, client->TxBuffer, sizeof(u32));
	return SendResponse(client->socket, result, client->TxBuffer, sizeof(u32));
}

static int InitDefaultOSDResources(struct ClientData *client, void *buffer, unsigned int length)
{
	int result;
	struct OSDResourceConfigContext *OSDConfigContext;

	if(client->task==TASK_NONE)
	{
		if((client->TaskData=malloc(sizeof(struct OSDResourceConfigContext)))!=NULL)
		{
			OSDConfigContext=client->TaskData;

			strncpy(OSDConfigContext->partition, buffer, sizeof(OSDConfigContext->partition)-1);
			OSDConfigContext->partition[sizeof(OSDConfigContext->partition)-1]='\0';
			OSDConfigContext->NewFileResourceIndex=-1;
			OSDConfigContext->NewFileBuffer=NULL;
			OSDConfigContext->AllocatedBufferLength=0;

			PrepareOSDDefaultResources(OSDConfigContext->resources);
			client->task=TASK_OSD_RESOURCE_CONFIG;
			result=0;
		}
		else result=-ENOMEM;
	}
	else result=-EBUSY;

	return SendResponse(client->socket, result, NULL, 0);
}

static int InitOSDResources(struct ClientData *client, void *buffer, unsigned int length)
{
	int result;
	struct OSDResourceConfigContext *OSDConfigContext;
	wchar_t OSDTitleLine1[OSD_TITLE_MAX_LEN+1], OSDTitleLine2[OSD_TITLE_MAX_LEN+1];
	mcIcon McSaveIconSys;
	char SaveFolderPath[38];

	WaitSema(client->StateSemaID);

	if(client->task==TASK_NONE)
	{
		if((client->TaskData=malloc(sizeof(struct OSDResourceConfigContext)))!=NULL)
		{
			OSDConfigContext=client->TaskData;

			strncpy(OSDConfigContext->partition, ((struct OSDResourceInitReq*)buffer)->partition, sizeof(OSDConfigContext->partition)-1);
			OSDConfigContext->partition[sizeof(OSDConfigContext->partition)-1]='\0';
			OSDConfigContext->NewFileResourceIndex=-1;
			OSDConfigContext->NewFileBuffer=NULL;
			OSDConfigContext->AllocatedBufferLength=0;

			mbstowcs(OSDTitleLine1, ((struct OSDResourceInitReq*)buffer)->OSDTitleLine1, OSD_TITLE_MAX_LEN+1);
			mbstowcs(OSDTitleLine2, ((struct OSDResourceInitReq*)buffer)->OSDTitleLine2, OSD_TITLE_MAX_LEN+1);

			if(((struct OSDResourceInitReq*)buffer)->UseSaveData && (LoadMcSaveSys(SaveFolderPath, &McSaveIconSys, ((struct OSDResourceInitReq*)buffer)->DiscID)>=0))
				result=PrepareOSDResources(SaveFolderPath, &McSaveIconSys, OSDTitleLine1, OSDTitleLine2, OSDConfigContext->resources);
			else
				result=PrepareOSDResources(NULL, NULL, OSDTitleLine1, OSDTitleLine2, OSDConfigContext->resources);

			client->task=TASK_OSD_RESOURCE_CONFIG;
		}
		else result=-ENOMEM;
	}
	else result=-EBUSY;

	SignalSema(client->StateSemaID);

	return SendResponse(client->socket, result, NULL, 0);
}

static int OSDResourceLoadInit(struct ClientData *client, void *buffer, unsigned int length)
{
	int result;
	struct OSDResourceConfigContext *OSDConfigContext;

	if(client->task==TASK_OSD_RESOURCE_CONFIG)
	{
		OSDConfigContext=client->TaskData;

		if(((struct OSDResourceWriteReq*)buffer)->index>=0 && ((struct OSDResourceWriteReq*)buffer)->index<NUM_OSD_FILES_ENTS)
		{
			if(OSDConfigContext->NewFileBuffer==NULL)
			{
				if((OSDConfigContext->NewFileBuffer=memalign(64, ((struct OSDResourceWriteReq*)buffer)->length))!=NULL)
				{
					OSDConfigContext->AllocatedBufferLength=((struct OSDResourceWriteReq*)buffer)->length;
					OSDConfigContext->NewFileResourceIndex=((struct OSDResourceWriteReq*)buffer)->index;

					SendResponse(client->socket, 0, NULL, 0);

					if((result = GetResponse(client->socket, OSDConfigContext->NewFileBuffer, OSDConfigContext->AllocatedBufferLength))==0)
					{
						if((result=LoadOSDResource(OSDConfigContext->NewFileResourceIndex, OSDConfigContext->resources, OSDConfigContext->NewFileBuffer, OSDConfigContext->AllocatedBufferLength, 1))!=0)
							free(OSDConfigContext->NewFileBuffer);
					}
					else
						free(OSDConfigContext->NewFileBuffer);

					OSDConfigContext->AllocatedBufferLength=0;
					OSDConfigContext->NewFileBuffer=NULL;
					OSDConfigContext->NewFileResourceIndex=-1;
				}
				else result=-ENOMEM;
			}
			else result=-EBUSY;
		}
		else result=-EINVAL;
	}
	else result=-EINVAL;

	return SendResponse(client->socket, result, NULL, 0);
}

static int WriteOSDResources(struct ClientData *client, void *buffer, unsigned int length)
{
	int result;
	char PartName[40];
	struct OSDResourceConfigContext *OSDConfigContext;

	if(client->task==TASK_OSD_RESOURCE_CONFIG)
	{
		OSDConfigContext=client->TaskData;
		snprintf(PartName, sizeof(PartName), "hdd0:%s", OSDConfigContext->partition);

		result=InstallOSDFiles(PartName, OSDConfigContext->resources);
		FreeOSDResources(OSDConfigContext->resources);

		free(OSDConfigContext);
		client->TaskData=NULL;
		client->task=TASK_NONE;
	}
	else result=-EINVAL;

	return SendResponse(client->socket, result, NULL, 0);
}

static int CancelOSDResourceWrite(struct ClientData *client, void *buffer, unsigned int length)
{
	int result;
	struct OSDResourceConfigContext *OSDConfigContext;

	WaitSema(client->StateSemaID);

	if(client->task==TASK_OSD_RESOURCE_CONFIG)
	{
		OSDConfigContext=client->TaskData;
		FreeOSDResources(OSDConfigContext->resources);

		free(OSDConfigContext);
		client->TaskData=NULL;
		client->task=TASK_NONE;
		result=0;
	}
	else result=-EINVAL;

	SignalSema(client->StateSemaID);

	return SendResponse(client->socket, result, NULL, 0);
}

static int GetOSDResourceStat(struct ClientData *client, void *buffer, unsigned int length)
{
	int result, i;
	char PartName[40];
	void *RespPayload;
	unsigned int RespPayloadLength;
	OSDResFileEnt_t ResourceFiles[NUM_OSD_FILES_ENTS];
	
	snprintf(PartName, sizeof(PartName), "hdd0:%s", ((struct OSDResourceStatReq*)buffer)->partition);
	if((result=GetOSDResourceFileStats(PartName, ResourceFiles))==0)
	{
		for(i=0; i<NUM_OSD_FILES_ENTS; i++)
			((struct OSDResourceStat*)client->TxBuffer)->lengths[i]=ResourceFiles[i].size;

		RespPayload=client->TxBuffer;
		RespPayloadLength=sizeof(struct OSDResourceStat);
	}
	else
	{
		RespPayload=NULL;
		RespPayloadLength=0;
	}

	return SendResponse(client->socket, result, RespPayload, RespPayloadLength);
}

static int ReadOSDResource(struct ClientData *client, void *buffer, unsigned int length)
{
	int result;
	char PartName[40];
	void *RespPayload, *tempbuffer;
	unsigned int RespPayloadLength;
	OSDResFileEnt_t ResourceFiles[NUM_OSD_FILES_ENTS];

	RespPayload=NULL;
	RespPayloadLength=0;
	tempbuffer=NULL;
	snprintf(PartName, sizeof(PartName), "hdd0:%s", ((struct OSDResourceReadReq*)buffer)->partition);
	if((result=GetOSDResourceFileStats(PartName, ResourceFiles))==0)
	{
		RespPayloadLength=ResourceFiles[((struct OSDResourceReadReq*)buffer)->index].size;
		if((tempbuffer=memalign(64, RespPayloadLength))!=NULL)
		{
			if((result=ReadOSDResourceFile(PartName, ((struct OSDResourceReadReq*)buffer)->index, ResourceFiles, tempbuffer))==0)
				RespPayload=tempbuffer;
		}
		else result=-ENOMEM;
	}

	result=SendResponse(client->socket, result, RespPayload, RespPayloadLength);
	if(tempbuffer!=NULL) free(tempbuffer);
	return result;
}

static int ReadOSDResourceTitles(struct ClientData *client, void *buffer, unsigned int length)
{
	int result;
	void *RespPayload;
	unsigned int RespPayloadLength;
	struct OSD_TitlesTransit OSDTitles;
	struct IconSysData IconSys;

	if((result=GetGameInstallationOSDIconSys(buffer, &IconSys))==0)
	{
		wcstombs(OSDTitles.OSDTitleLine1, IconSys.title0, sizeof(OSDTitles.OSDTitleLine1));
		wcstombs(OSDTitles.OSDTitleLine2, IconSys.title1, sizeof(OSDTitles.OSDTitleLine2));

		RespPayload=&OSDTitles;
		RespPayloadLength=sizeof(OSDTitles);
	}
	else
	{
		RespPayload=NULL;
		RespPayloadLength=0;
	}

	return SendResponse(client->socket, result, RespPayload, RespPayloadLength);
}

static int CheckMCSave(struct ClientData *client, void *buffer, unsigned int length)
{
	return SendResponse(client->socket, CheckExistingMcSave(((struct OSD_MC_ResourceStatReq*)buffer)->DiscID), NULL, 0);
}

static int GetMCSaveResourceStat(struct ClientData *client, void *buffer, unsigned int length)
{
	char filename[33];
	int result;
	iox_stat_t FileStat;
	
	strncpy(filename, ((struct OSD_MC_ResourceReq*)buffer)->filename, sizeof(filename)-1);
	filename[sizeof(filename)-1]='\0';
	if((result=GetExistingFileInMcSaveStat(((struct OSD_MC_ResourceReq*)buffer)->DiscID, filename, &FileStat))>=0)
		result=FileStat.size;

	return SendResponse(client->socket, result, NULL, 0);
}

static int ReadMCSaveResource(struct ClientData *client, void *buffer, unsigned int length)
{
	void *tempbuffer, *RespPayload;
	unsigned int RespPayloadLength;
	char filename[33];
	int result;

	RespPayload=NULL;
	RespPayloadLength=0;
	if((tempbuffer=memalign(64, ((struct OSD_MC_ResourceReadReq*)buffer)->length))!=NULL)
	{
		strncpy(filename, ((struct OSD_MC_ResourceReadReq*)buffer)->filename, sizeof(filename)-1);
		filename[sizeof(filename)-1]='\0';
		if((result=ReadExistingFileInMcSave(((struct OSD_MC_ResourceReadReq*)buffer)->DiscID, filename, tempbuffer, ((struct OSD_MC_ResourceReadReq*)buffer)->length))>=0)
		{
			RespPayload=tempbuffer;
			RespPayloadLength=((struct OSD_MC_ResourceReadReq*)buffer)->length;
		}
	}
	else result=-ENOMEM;

	result=SendResponse(client->socket, result, RespPayload, RespPayloadLength);
	if(tempbuffer!=NULL) free(tempbuffer);
	return result;
}

static int PowerOffServer(struct ClientData *client, void *buffer, unsigned int length)
{
	if(NumClientsConnected <= 1)
	{
		//Shut down DEV9
		fileXioDevctl("dev9x:", DDIOC_OFF, NULL, 0, NULL, 0);

		poweroffShutdown();
		return 0;
	} else
		return -EBUSY;
}

static int HandlePacket(struct ClientData *client, unsigned int command, unsigned char *buffer, unsigned int length)
{
	int result;
	const void *RespPayload;
	unsigned int RespPayloadLength;

	if((client->status&CLIENT_STATUS_CONNECTED) && !(client->status&CLIENT_STATUS_VERSION_VERIFIED))
	{
		//By default, there is no payload to send with the response packet as most functions won't send one.
		RespPayload=NULL;
		RespPayloadLength=0;

		switch(command)
		{
			case HDLGMAN_SERVER_GET_VERSION:
				*(int *)client->TxBuffer=HDLGMAN_SERVER_VERSION;
				result=0;
				RespPayload=client->TxBuffer;
				RespPayloadLength=4;
				break;
			case HDLGMAN_CLIENT_SEND_VERSION:
				if(*(unsigned int *)buffer==HDLGMAN_CLIENT_VERSION)
				{
					result=0;
					client->status|=CLIENT_STATUS_VERSION_VERIFIED;
				}
				else{
					result=HDLGMAN_CLIENT_VERSION_ERR;
				}

				break;
			default:
				printf("Unrecognized pre-verification command: 0x%04x\n", command);
				result=-EINVAL;
		}

		result=SendResponse(client->socket, result, RespPayload, RespPayloadLength);
	}
	else if((client->status)&(CLIENT_STATUS_CONNECTED|CLIENT_STATUS_VERSION_VERIFIED))
	{
		switch(command)
		{
			case HDLGMAN_SERVER_PREP_GAME_INST:
				result=PrepareGameInstallation(client, buffer, length);
				break;
			case HDLGMAN_SERVER_INIT_GAME_READ:
				result=InitGameRead(client, buffer, length);
				break;
			case HDLGMAN_SERVER_INIT_GAME_WRITE:
				result=InitGameWrite(client, buffer, length);
				break;
			case HDLGMAN_SERVER_IO_STATUS:
				result=GetIOStatus(client);
				break;
			case HDLGMAN_SERVER_CLOSE_GAME:
				result=HandleCloseGame(client);
				break;
			case HDLGMAN_SERVER_LOAD_GAME_LIST:
				result=LoadGameList(client, buffer, length);
				break;
			case HDLGMAN_SERVER_READ_GAME_LIST:
				result=ReadGameList(client, buffer, length);
				break;
			case HDLGMAN_SERVER_READ_GAME_LIST_ENTRY:
				result=ReadGameListEntry(client, buffer, length);
				break;
			case HDLGMAN_SERVER_READ_GAME_ENTRY:
				result=ReadGameListEntryDetails(client, buffer, length);
				break;
			case HDLGMAN_SERVER_UPD_GAME_ENTRY:
				result=UpdateGameListEntry(client, buffer, length);
				break;
			case HDLGMAN_SERVER_DEL_GAME_ENTRY:
				result=DeleteGameEntry(client, buffer, length);
				break;
			case HDLGMAN_SERVER_GET_GAME_PART_NAME:
				result=GetPartName(client, buffer, length);
				break;
			case HDLGMAN_SERVER_GET_FREE_SPACE:
				result=GetFreeSpace(client);
				break;
			case HDLGMAN_SERVER_INIT_DEFAULT_OSD_RESOURCES:
				result=InitDefaultOSDResources(client, buffer, length);
				break;
			case HDLGMAN_SERVER_INIT_OSD_RESOURCES:
				result=InitOSDResources(client, buffer, length);
				break;
			case HDLGMAN_SERVER_OSD_RES_LOAD_INIT:
				result=OSDResourceLoadInit(client, buffer, length);
				break;
			case HDLGMAN_SERVER_WRITE_OSD_RESOURCES:
				result=WriteOSDResources(client, buffer, length);
				break;
			case HDLGMAN_SERVER_OSD_RES_WRITE_CANCEL:
				result=CancelOSDResourceWrite(client, buffer, length);
				break;
			case HDLGMAN_SERVER_GET_OSD_RES_STAT:
				result=GetOSDResourceStat(client, buffer, length);
				break;
			case HDLGMAN_SERVER_OSD_RES_READ:
				result=ReadOSDResource(client, buffer, length);
				break;
			case HDLGMAN_SERVER_OSD_RES_READ_TITLES:
				result=ReadOSDResourceTitles(client, buffer, length);
				break;
			case HDLGMAN_SERVER_OSD_MC_SAVE_CHECK:
				result=CheckMCSave(client, buffer, length);
				break;
			case HDLGMAN_SERVER_OSD_MC_GET_RES_STAT:
				result=GetMCSaveResourceStat(client, buffer, length);
				break;
			case HDLGMAN_SERVER_OSD_MC_RES_READ:
				result=ReadMCSaveResource(client, buffer, length);
				break;
			case HDLGMAN_SERVER_SHUTDOWN:
				result=PowerOffServer(client, buffer, length);
				break;
			default:
				printf("Unrecognized post-verification command: 0x%04x\n", command);
				result=-EINVAL;
		}
	}
	else{
		printf("Illegal client connection state.\n");
		result=-1;
	}

	return result;
}
