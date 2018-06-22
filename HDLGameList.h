struct HDLGameEntry{
	char PartName[33];
	char DiscID[11];
	u8 CompatibilityModeFlags;
	u8 TRType;
	u8 TRMode;
	u8 DiscType;
	u32 sectors;
	char GameTitle[GAME_TITLE_MAX_LEN_BYTES+1];
};

/* Function prototypes */
void LockCentralHDLGameList(void);
void UnlockCentralHDLGameList(void);
void InitCentralHDLGameList(void);
void DeinitCentralHDLGameList(void);
int LoadHDLGameList(struct HDLGameEntry **GameList);
unsigned int GetHDLGameListGeneration(void);
int GetHDLGameList(struct HDLGameEntry **GameList);
void FreeHDLGameList(void);
