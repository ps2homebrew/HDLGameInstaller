enum OSD_RESOURCE_FILE_TYPES{
	OSD_SYSTEM_CNF_INDEX	= 0,
	OSD_ICON_SYS_INDEX,
	OSD_VIEW_ICON_INDEX,
	OSD_DEL_ICON_INDEX,
	OSD_BOOT_KELF_INDEX,

	NUM_OSD_FILES_ENTS
};

#define HDL_GAME_DATA_OFFSET		0x100000	/* Starts at sector 0x800, and spans across sectors 0x800 and 0x801. */

struct GameSettings{
	wchar_t FullTitle[GAME_TITLE_MAX_LEN+1];
	wchar_t OSDTitleLine1[OSD_TITLE_MAX_LEN+1];
	wchar_t OSDTitleLine2[OSD_TITLE_MAX_LEN+1];
	unsigned char CompatibilityModeFlags;
	unsigned char UseMDMA0;
};

typedef struct{
	int offset;
	int size;
}OSDResFileEnt_t;

typedef struct PartAttributeAreaTable{
	char magic[9];	/* "PS2ICON3D" */
	unsigned char reserved[3];
	int version;	/* Must be zero. */
	OSDResFileEnt_t FileEnt[NUM_OSD_FILES_ENTS];
	unsigned char reserved2[456];
} t_PartAttrTab __attribute__((packed));

struct OSDResourceFileEntry{
	const char *FileName;
	void *buffer;
	int size;
	unsigned int flags;
};

struct IconSysData{
	wchar_t title0[OSD_TITLE_MAX_LEN+1];
	wchar_t title1[OSD_TITLE_MAX_LEN+1];
	unsigned char bgcola;
	unsigned char bgcol0[3];
	unsigned char bgcol1[3];
	unsigned char bgcol2[3];
	unsigned char bgcol3[3];
	float lightdir0[3];
	float lightdir1[3];
	float lightdir2[3];
	unsigned char lightcolamb[3];
	unsigned char lightcol0[3];
	unsigned char lightcol1[3];
	unsigned char lightcol2[3];
	wchar_t uninstallmes0[121];
	wchar_t uninstallmes1[121];
	wchar_t uninstallmes2[121];
};

struct IconInfo{
	char foldername[33];
	wchar_t title[33];
};

#define FILE_FLAGS_OPTIONAL	0x01
#define FILE_FLAGS_ALLOCATED	0x02	//Buffer was allocated and should be free when no longer needed.

enum ICON_SOURCE{
	ICON_SOURCE_DEFAULT	= 0,
	ICON_SOURCE_SAVE,
	ICON_SOURCE_EXTERNAL
};

void InitOSDResourceFiles(void);
void ConvertMcTitle(const mcIcon *icon, wchar_t *title1, wchar_t *title2);
int CheckExistingMcSave(const char* DiscID);
int ReadExistingFileInMcSave(const char *DiscID, const char *filename, void *buffer, unsigned int length);
int LoadMcSaveSysFromPath(const char* SaveFolderPath, mcIcon* McSaveIconSys);
int LoadMcSaveSys(char* SaveFolderPath, mcIcon* McSaveIconSys, const char* DiscID);
int PrepareOSDResources(const char* SaveFolderPath, const mcIcon* McSaveIconSys, const wchar_t *OSDTitleLine1, const wchar_t *OSDTitleLine2, struct OSDResourceFileEntry *InstFileList);
int LoadOSDResource(unsigned int index, struct OSDResourceFileEntry *InstFileList, void *buffer, unsigned int length, unsigned char IsAllocatedBuffer);
void FreeOSDResources(struct OSDResourceFileEntry *InstFileList);
int LoadIconSysFile(const unsigned char *buffer, int size, struct IconSysData *data);
int GetOSDResourceFileStats(const char *partition, OSDResFileEnt_t *files);
int GetExistingFileInMcSaveStat(const char *DiscID, const char *filename, iox_stat_t *stat);
int ReadOSDResourceFile(const char *partition, int index, const OSDResFileEnt_t *files, void *buffer);
int InstallOSDFiles(const char *partition, struct OSDResourceFileEntry *InstFileList);
void PrepareOSDDefaultResources(struct OSDResourceFileEntry *InstFileList);
int InstallGameInstallationOSDResources(const char *partition, const struct GameSettings *GameSettings, const mcIcon *McSaveIconSys, const char *SaveDataPath);
int UpdateGameInstallationOSDResources(const char *partition, const wchar_t *OSDTitleLine1, const wchar_t *OSDTitleLine2);
int GetGameInstallationOSDIconSys(const char *partition, struct IconSysData *IconSys);
int GetIconListFromDevice(const char *device, struct IconInfo **IconList);
