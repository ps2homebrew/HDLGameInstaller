struct GameSettings{
	wchar_t FullTitle[GAME_TITLE_MAX_LEN+1];
	wchar_t OSDTitleLine1[OSD_TITLE_MAX_LEN+1];
	wchar_t OSDTitleLine2[OSD_TITLE_MAX_LEN+1];
	unsigned char CompatibilityModeFlags;
	unsigned char UseMDMA0;
	unsigned char DiscType;
	unsigned char IconSource;
	unsigned char source;
	wchar_t *SourcePath;
	wchar_t *IconSourcePath;	//Valid and only used if IconSource=2.
};

struct JobParams{
	int count;
	struct GameSettings *games;
};

struct InstallerThreadParams{
	HWND ParentDialog;
	struct JobParams *jobs;
};

struct RetrieveThreadParams{
	HWND ParentDialog;
	u32 sectors;
	char partition[33];
	wchar_t *destination;
};

enum InstallerThreadRemoteCommandValues{
	INSTALLER_CMD_NONE=0,
	INSTALLER_CMD_ABORT,
};

void StartInstallation(struct InstallerThreadParams *params);
void StartCopy(struct RetrieveThreadParams *params);
