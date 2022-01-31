void InitPads(void);
void DeinitPads(void);
int ReadPadStatus_raw(int port, int slot);
int ReadCombinedPadStatus_raw(void);
void WaitPadClear(int port, int slot);
int ReadPadStatus(int port, int slot);
int ReadCombinedPadStatus(void);
