#include <errno.h>
#include <stdio.h>
#include <winsock2.h>

#include "HDLGManClient.h"
#include "system.h"
#include "client.h"
#include "main.h"
#include "io.h"

static struct sockaddr_in ServerSocketData;
static SOCKET connection = INVALID_SOCKET;
static SOCKET dataConnection = INVALID_SOCKET;

static int DoSend(SOCKET s, void *buffer, int length)
{
	int result, remaining;
	char *ptr;

	for(ptr = (char*)buffer,remaining = length,result = 0; remaining > 0; remaining -= result,ptr += result)
	{
		if((result=send(s, ptr, remaining, 0)) <= 0)
		{
		//	rcode = WSAGetLastError();
			result=-EEXTCONNLOST;
			break;
		}
	}

	if(result > 0) result = length;

	return result;
}

int SendCmdPacket(const void *buffer, unsigned int command, int NumBytes)
{
	int result;
	struct HDDToolsPacketHdr header;

	header.command=command;
	header.result=0;
	header.PayloadLength=NumBytes;

	if((result=DoSend(connection, (void*)&header, sizeof(header)))==sizeof(header))
	{
		if(NumBytes > 0)
			result = DoSend(connection, (void*)buffer, NumBytes);
		else
			result = 0;
	}

	return result;
}

int SendData(const void *buffer, int NumBytes)
{
	return DoSend(dataConnection, (void*)buffer, NumBytes);
}

static int DoRecv(SOCKET s, void *buffer, int length)
{
	struct timeval timeout;
	int result, remaining;
	char *ptr;
	FD_SET readFDs;

	for(ptr = (char*)buffer,remaining = length,result = 0; remaining > 0; remaining -= result,ptr += result)
	{
		timeout.tv_sec = 30;
		timeout.tv_usec = 0;
		FD_ZERO(&readFDs);
		FD_SET(s, &readFDs);
		if(select(s + 1, &readFDs, NULL, NULL, &timeout) == 1)
		{
			if((result=recv(s, ptr, remaining, 0)) <= 0)
			{
			//	rcode = WSAGetLastError();
				result = -EEXTCONNLOST;
				break;
			}
		}else{
			result=-EEXTCONNLOST;
			DisconnectSocketConnection();
			break;
		}
	}

	if(result > 0) result = length;

	return result;
}

int GetResponse(void *buffer, int NumBytes)
{
	int result;
	struct HDDToolsPacketHdr header;

	//Receive header.
	if((result=DoRecv(connection, (void*)&header, sizeof(header)))==sizeof(header))
	{
		//Receive payload.
		if(NumBytes > 0 && header.PayloadLength > 0)
		{
			if((result = DoRecv(connection, buffer, (int)header.PayloadLength > NumBytes ? NumBytes : header.PayloadLength)) > 0)
				result = header.result;
		}else
			result = header.result;
	}

	return result;
}

int GetPayload(void *buffer, int NumBytes)
{
	return DoRecv(connection, buffer, NumBytes);
}

int RecvData(void *buffer, int NumBytes)
{
	return DoRecv(dataConnection, buffer, NumBytes);
}

int InitializeClientSys(void)
{
	// Must be done at the beginning of every WinSock program
	WSADATA w;	// used to store information about WinSock version

	if(WSAStartup (0x0202, &w))
	{ // there was an error
		return -EIO;
	}

	if (w.wVersion != 0x0202)
	{ // wrong WinSock version!
		WSACleanup(); // unload ws2_32.dll
		return -EINVAL;
	}

	return 0;
}

void DeinitializeClientSys(void){
	WSACleanup();
}

int ConnectToServer(const char *ipAddress)
{
	int result, value;

	connection=socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // Create socket

	ServerSocketData.sin_family = AF_INET;
	ServerSocketData.sin_port = htons(SERVER_PORT_NUM);
	ServerSocketData.sin_addr.s_addr = inet_addr(ipAddress);
	value = HDLGMAN_IO_BLOCK_SIZE * 2048;
	setsockopt(connection, SOL_SOCKET, SO_RCVBUF, (const char*)&value, sizeof(value));

	result=connect(connection, (SOCKADDR*)&ServerSocketData, sizeof(ServerSocketData));

	return(result>=0?0:result);
}

int PrepareDataConnection(void)
{
	struct sockaddr_in addr;
	SOCKET s;

	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVER_DATA_PORT_NUM);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if(bind(s, (struct sockaddr*)&addr, sizeof(addr))==0 && listen(s, 1)==0)
		return s;

	return INVALID_SOCKET;
}

int AcceptDataConnection(int s)
{
	SOCKET peer;
	struct timeval timeout;
	FD_SET readFDs;
	int result;

	timeout.tv_sec = 80;	//Give more time than a standard TCP client may take to connect.
	timeout.tv_usec = 0;
	FD_ZERO(&readFDs);
	FD_SET(s, &readFDs);
	if(select(s + 1, &readFDs, NULL, NULL, &timeout) == 1)
	{
		peer = accept(s, NULL, 0);
		result = 0;
	} else {
		result = -EEXTCONNLOST;
	}

	shutdown(s, SD_BOTH);
	closesocket(s);

	if(result >= 0)
	{
		dataConnection = peer;
		return 0;
	}

	return result;
}

void CloseDataConnection(void)
{
	char dummy;

	shutdown(dataConnection, SD_SEND);
	//Wait for remote to close the socket, which shall take place after all sent data has been received.
	while(recv(dataConnection, &dummy, sizeof(dummy), 0) > 0);
	closesocket(dataConnection);
}

void ClosePendingDataConnection(int s)
{
	shutdown(s, SD_SEND);
	closesocket(s);
}

int ReconnectToServer(void)
{
	int result, value;

	connection=socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // Create socket
	value = HDLGMAN_IO_BLOCK_SIZE * 2048;
	setsockopt(connection, SOL_SOCKET, SO_RCVBUF, (const char*)&value, sizeof(value));
	if((result=connect(connection, (SOCKADDR*)&ServerSocketData, sizeof(ServerSocketData))) >= 0)
	{
		result = ExchangeHandshakesWithServer();
	}

	return result;
}

int ReconnectToServerForRead(const char *partition, u32 sectors, u32 offset)
{
	s32 result;

	if ((result = HDLGManInitGameRead(partition, sectors, offset)) >= 0)
			result = 0;

	return result;
}

int ReconnectToServerForWrite(const char *DiscID, u32 sectors, u32 offset)
{
	s32 result;
	char partition[33];

	if((result = HDLGManGetGamePartName(DiscID, partition)) >= 0)
	{
		if ((result = HDLGManInitGameWrite(partition, sectors, offset)) >= 0)
			result = 0;
	}

	return result;
}

int DisconnectSocketConnection(void)
{
	shutdown(connection, SD_BOTH);
	return closesocket(connection);
}

int ExchangeHandshakesWithServer(void)
{
	int version, result;

//	printf("Exchanging handshakes...");

	version=-1;
	if((result=SendCmdPacket(NULL, HDLGMAN_SERVER_GET_VERSION, 0))>=0)
	{
		if((result=GetResponse(&version, sizeof(version)))==0)
		{
			if(version!=HDLGMAN_SERVER_VERSION)
			{
	//			printf("Error: Unsupported server version - 0x%04x\n", version);
				result = -EINVAL;
			}
			else{
				/* Now, send the client's version. */
				version=HDLGMAN_CLIENT_VERSION;
				SendCmdPacket(&version, HDLGMAN_CLIENT_SEND_VERSION, sizeof(version));
				if((result=GetResponse(NULL, 0))!=0)
				{
	//				printf("Server rejected connection, code: 0x%04x\n", result);
				}
	//			else printf("done\n");
			}
		}
		else
		{
	//		printf("Error: Unable to retrieve the server's version number: %d\n", result);
		}
	}
	else
	{
	//	printf("Error: Unable to send SERVER_GET_VERSION\n");
	}

	return result;
}
