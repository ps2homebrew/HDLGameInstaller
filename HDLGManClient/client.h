int SendCmdPacket(const void *buffer, unsigned int command, int NumBytes);	//Command
int SendData(const void *buffer, int NumBytes);	//Data
int GetResponse(void *buffer, int NumBytes);	//Command
int GetPayload(void *buffer, int NumBytes);		//Command
int RecvData(void *buffer, int NumBytes);		//Data
int ExchangeHandshakesWithServer(void);
int DisconnectSocketConnection(void);

int InitializeClientSys(void);
void DeinitializeClientSys(void);
int ConnectToServer(const char *ipAddress);
int PrepareDataConnection(void);
void CloseDataConnection(void);
void ClosePendingDataConnection(int s);
int AcceptDataConnection(int s);
int ReconnectToServerForWrite(const char *DiscID, u32 sectors, u32 offset);
int ReconnectToServerForRead(const char *partition, u32 sectors, u32 offset);
int ReconnectToServer(void);
