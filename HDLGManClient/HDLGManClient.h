#pragma pack(push, 1)

typedef unsigned int u32;
typedef unsigned short int u16;
typedef unsigned char u8;
typedef unsigned long long int u64;

enum OSD_RESOURCE_FILE_TYPES {
    OSD_SYSTEM_CNF_INDEX = 0,
    OSD_ICON_SYS_INDEX,
    OSD_VIEW_ICON_INDEX,
    OSD_DEL_ICON_INDEX,
    OSD_BOOT_KELF_INDEX,

    NUM_OSD_FILES_ENTS
};

typedef struct
{
    int offset;
    int size;
} OSDResFileEnt_t;

#define GAME_TITLE_MAX_LEN_BYTES 160                            // In bytes, when in UTF-8 characters.
#define GAME_TITLE_MAX_LEN       (GAME_TITLE_MAX_LEN_BYTES / 2) /*	In characters. Note: the original format for HDLoader just has a 160-character space. \
                                                                    But UTF-8 characters may have 1 or more bytes each. Hence this is an approximation.	*/
#define OSD_TITLE_MAX_LEN_BYTES  (OSD_TITLE_MAX_LEN * 4)        // In bytes, when in UTF-8 characters.
#define OSD_TITLE_MAX_LEN        16                             // In characters

typedef struct PartAttributeAreaTable
{
    char magic[9]; /* "PS2ICON3D" */
    u8 reserved[3];
    int version; /* Must be zero. */
    OSDResFileEnt_t FileEnt[NUM_OSD_FILES_ENTS];
    u8 reserved2[456];
} t_PartAttrTab; //__attribute__((packed));

/* Server configuration */
#define SERVER_PORT_NUM      45061
#define SERVER_DATA_PORT_NUM 45062

/* Transfer rate modes. */
#define ATA_XFER_MODE_PIO  0x08
#define ATA_XFER_MODE_MDMA 0x20
#define ATA_XFER_MODE_UDMA 0x40

struct HDDToolsPacketHdr
{
    u32 command;
    u32 PayloadLength;
    int result;
};

#define SET_COMPAT_MODE_1(var, value) (var = ((var & ~0x01) | (value ? 1 : 0) << 0))
#define SET_COMPAT_MODE_2(var, value) (var = ((var & ~0x02) | (value ? 1 : 0) << 1))
#define SET_COMPAT_MODE_3(var, value) (var = ((var & ~0x04) | (value ? 1 : 0) << 2))
#define SET_COMPAT_MODE_4(var, value) (var = ((var & ~0x08) | (value ? 1 : 0) << 3))
#define SET_COMPAT_MODE_5(var, value) (var = ((var & ~0x10) | (value ? 1 : 0) << 4))
#define SET_COMPAT_MODE_6(var, value) (var = ((var & ~0x20) | (value ? 1 : 0) << 5))
#define SET_COMPAT_MODE_7(var, value) (var = ((var & ~0x40) | (value ? 1 : 0) << 6))
#define SET_COMPAT_MODE_8(var, value) (var = ((var & ~0x80) | (value ? 1 : 0) << 7))

#define GET_COMPAT_MODE_1(var) (var >> 0 & 1)
#define GET_COMPAT_MODE_2(var) (var >> 1 & 1)
#define GET_COMPAT_MODE_3(var) (var >> 2 & 1)
#define GET_COMPAT_MODE_4(var) (var >> 3 & 1)
#define GET_COMPAT_MODE_5(var) (var >> 4 & 1)
#define GET_COMPAT_MODE_6(var) (var >> 5 & 1)
#define GET_COMPAT_MODE_7(var) (var >> 6 & 1)
#define GET_COMPAT_MODE_8(var) (var >> 7 & 1)

struct HDLGameInfo
{
    char GameTitle[GAME_TITLE_MAX_LEN_BYTES + 1];
    char DiscID[11];       /* E.g. SXXX-99999 */
    char StartupFname[14]; /* E.g. "SXXX-999.99;1" */
    u8 DiscType;
    u32 SectorsInDiscLayer0;
    u32 SectorsInDiscLayer1;
    u8 CompatibilityFlags;
    u8 TRType;
    u8 TRMode;
};

struct IOInitReq
{
    u32 sectors, offset;
    char partition[33];
};

struct OSDResourceInitReq
{
    int UseSaveData;
    char OSDTitleLine1[OSD_TITLE_MAX_LEN_BYTES + 1]; // 16 UTF-8 characters maximum.
    char OSDTitleLine2[OSD_TITLE_MAX_LEN_BYTES + 1]; // 16 UTF-8 characters maximum.
    char DiscID[11];
    char partition[33];
};

struct OSDResourceWriteReq
{
    int index;
    u32 length;
};

struct OSDResourceStat
{
    u32 lengths[NUM_OSD_FILES_ENTS];
};

struct OSDResourceStatReq
{
    char partition[33];
};

struct OSDResourceReadReq
{
    char partition[33];
    int index;
};

struct OSD_TitlesTransit
{
    char title1[OSD_TITLE_MAX_LEN_BYTES + 1];
    char title2[OSD_TITLE_MAX_LEN_BYTES + 1];
};

struct OSD_MC_ResourceStatReq
{
    char DiscID[11];
};

struct OSD_MC_ResourceReq
{
    char DiscID[11];
    char filename[32];
};

struct OSD_MC_ResourceReadReq
{
    char DiscID[11];
    char filename[32];
    u32 length;
};

struct HDLGameEntryTransit
{
    char PartName[33];
    char GameTitle[GAME_TITLE_MAX_LEN_BYTES + 1];
    char DiscID[11];
    u8 CompatibilityModeFlags;
    u8 TRType;
    u8 TRMode;
    u8 DiscType;
    u32 sectors; // In 2048-byte units
};

#pragma pack(pop)

#define HDLGMAN_SERVER_VERSION 0x0C
#define HDLGMAN_CLIENT_VERSION 0x0C

enum HDLGMAN_ClientServerCommands {
    // Server commands
    HDLGMAN_SERVER_RESPONSE = 0,
    HDLGMAN_SERVER_GET_VERSION,

    // Client commands
    HDLGMAN_CLIENT_SEND_VERSION,
    HDLGMAN_CLIENT_VERSION_ERR, // Server rejects connection to client (version mismatch).

    // Game installation commands
    HDLGMAN_SERVER_PREP_GAME_INST = 0x10, // Creates a new partition and will also automatically invoke HDLGMAN_SERVER_INIT_GAME_WRITE.
    HDLGMAN_SERVER_INIT_GAME_WRITE,       // Opens an existing partition for writing.
    HDLGMAN_SERVER_INIT_GAME_READ,        // Opens an existing partition for reading.
    HDLGMAN_SERVER_IO_STATUS,
    HDLGMAN_SERVER_CLOSE_GAME,
    HDLGMAN_SERVER_LOAD_GAME_LIST,
    HDLGMAN_SERVER_READ_GAME_LIST,
    HDLGMAN_SERVER_READ_GAME_LIST_ENTRY,
    HDLGMAN_SERVER_READ_GAME_ENTRY,
    HDLGMAN_SERVER_UPD_GAME_ENTRY,
    HDLGMAN_SERVER_DEL_GAME_ENTRY,
    HDLGMAN_SERVER_GET_GAME_PART_NAME,
    HDLGMAN_SERVER_GET_FREE_SPACE,

    // OSD-resource management
    HDLGMAN_SERVER_INIT_OSD_RESOURCES = 0x20,
    HDLGMAN_SERVER_OSD_RES_LOAD_INIT,
    HDLGMAN_SERVER_OSD_RES_LOAD,
    HDLGMAN_SERVER_WRITE_OSD_RESOURCES,
    HDLGMAN_SERVER_OSD_RES_WRITE_CANCEL,
    HDLGMAN_SERVER_GET_OSD_RES_STAT,
    HDLGMAN_SERVER_OSD_RES_READ,
    HDLGMAN_SERVER_OSD_RES_READ_TITLES,
    HDLGMAN_SERVER_INIT_DEFAULT_OSD_RESOURCES, // Same as HDLGMAN_SERVER_INIT_OSD_RESOURCES, but doesn't require the OSD titles and is designed for subsequent pre-built OSD resource file uploading (with HDLGMAN_SERVER_OSD_RES_LOAD) from the client.
    HDLGMAN_SERVER_OSD_MC_SAVE_CHECK = 0x40,
    HDLGMAN_SERVER_OSD_MC_GET_RES_STAT,
    HDLGMAN_SERVER_OSD_MC_RES_READ,

    HDLGMAN_SERVER_SHUTDOWN = 0xFF,
};

#define HDLGMAN_IO_BLOCK_SIZE IO_BANKSIZE

int HDLGManPrepareGameInstall(const wchar_t *GameTitle, const char *DiscID, const char *StartupFname, u8 DiscType, u32 SectorsInDiscLayer0, u32 SectorsInDiscLayer1, u8 CompatibilityModeFlags, u8 TRType, u8 TRMode);
int HDLGManWriteGame(const void *buffer, u32 NumBytes);
int HDLGManInitGameWrite(const char *partition, u32 sectors, u32 offset); // To open a created game for writing.
int HDLGManInitGameRead(const char *partition, u32 sectors, u32 offset);  // To open a created game for reading.
int HDLGManGetIOStatus(void);
int HDLGManReadGame(void *buffer, u32 NumBytes);
int HDLGManCloseGame(void);
int HDLGManLoadGameList(struct HDLGameEntry **GameList);
int HDLGManLoadGameListEntry(struct HDLGameEntry *GameListEntry, int index);
int HDLGManReadGameEntry(const char *partition, struct HDLGameEntry *GameEntry);
int HDLGManUpdateGameEntry(struct HDLGameEntry *GameEntry);
int HDLGManDeleteGameEntry(const char *partition);
int HDLGManGetGamePartName(const char *DiscID, char *partition);
unsigned long int HDLGManGetFreeSpace(void);

int HDLGManInitOSDDefaultResources(const char *partition);
int HDLGManInitOSDResources(const char *partition, const char *DiscID, const wchar_t *OSDTitleLine1, const wchar_t *OSDTitleLine2, int UseSaveData);
int HDLGManOSDResourceLoad(int index, const void *buffer, u32 length);
int HDLGManWriteOSDResources(void);
int HDLGManOSDResourceWriteCancel(void);
int HDLGManGetOSDResourcesStat(const char *partition, struct OSDResourceStat *stat);
int HDLGManReadOSDResourceFile(const char *partition, int index, void *buffer, u32 length);
int HDLGManGetGameInstallationOSDTitles(const char *partition, struct OSD_Titles *titles);

int HDLGManCheckExistingMCSaveData(const char *DiscID);
int HDLGManGetMCSaveDataFileStat(const char *DiscID, const char *filename);
int HDLGManReadMCSaveDataFile(const char *DiscID, const char *filename, void *buffer, u32 length);

int HDLGManPowerOffServer(void);
