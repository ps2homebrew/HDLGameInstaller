/* Server configuration */
#define MAX_CLIENTS			1
#define SERVER_PORT_NUM			45061
#define SERVER_DATA_PORT_NUM		45062
#define SERVER_MAIN_THREAD_PRIORITY	0x10
#define SERVER_CLIENT_THREAD_PRIORITY	0x11
#define SERVER_IO_THREAD_PRIORITY	0x12
#define SERVER_THREAD_STACK_SIZE	0xA00
#define CLIENT_THREAD_STACK_SIZE	0xA00
#define CLIENT_DATA_THREAD_STACK_SIZE	0xA00

struct HDDToolsPacketHdr{
	u32 command;
	u32 PayloadLength;
	int result;
} __attribute__((__packed__));

struct HDLGameInfo{
	char GameTitle[GAME_TITLE_MAX_LEN_BYTES+1];
	char DiscID[11];	/* E.g. SXXX-99999 */
	char StartupFname[14];	/* E.g. "SXXX-999.99;1" */
	u8 DiscType;
	u32 SectorsInDiscLayer0;
	u32 SectorsInDiscLayer1;
	u8 CompatibilityFlags;
	u8 TRType;
	u8 TRMode;
} __attribute__((__packed__));

struct IOInitReq{
	u32 sectors, offset;
	char partition[33];
} __attribute__((__packed__));

struct HDLGameEntryTransit{
	char PartName[33];
	char GameTitle[GAME_TITLE_MAX_LEN_BYTES+1];
	char DiscID[11];
	u8 CompatibilityModeFlags;
	u8 TRType;
	u8 TRMode;
	u8 DiscType;
	u32 sectors;	//In 2048-byte units
} __attribute__((__packed__));

struct OSDResourceInitReq{
	int UseSaveData;
	char OSDTitleLine1[OSD_TITLE_MAX_LEN_BYTES+1];	// 16 characters maximum.
	char OSDTitleLine2[OSD_TITLE_MAX_LEN_BYTES+1];	// 16 characters maximum.
	char DiscID[11];
	char partition[33];
} __attribute__((__packed__));

struct OSDResourceWriteReq{
	int index;
	u32 length;
} __attribute__((__packed__));

struct OSDResourceStat{
	u32 lengths[NUM_OSD_FILES_ENTS];
} __attribute__((__packed__));

struct OSDResourceStatReq{
	char partition[33];
} __attribute__((__packed__));

struct OSDResourceReadReq{
	char partition[33];
	int index;
} __attribute__((__packed__));

struct OSD_TitlesTransit{
	char OSDTitleLine1[OSD_TITLE_MAX_LEN_BYTES+1];	// 16 characters maximum.
	char OSDTitleLine2[OSD_TITLE_MAX_LEN_BYTES+1];	// 16 characters maximum.
} __attribute__((__packed__));

struct OSD_MC_ResourceStatReq{
	char DiscID[11];
} __attribute__((__packed__));

struct OSD_MC_ResourceReq{
	char DiscID[11];
	char filename[32];
} __attribute__((__packed__));

struct OSD_MC_ResourceReadReq{
	char DiscID[11];
	char filename[32];
	u32 length;
} __attribute__((__packed__));

#define HDLGMAN_SERVER_VERSION		0x0C
#define HDLGMAN_CLIENT_VERSION		0x0C

enum HDLGMAN_ClientServerCommands{
	//Server commands
	HDLGMAN_SERVER_RESPONSE		=	0x00,
	HDLGMAN_SERVER_GET_VERSION,

	//Client commands
	HDLGMAN_CLIENT_SEND_VERSION,
	HDLGMAN_CLIENT_VERSION_ERR,		//Server rejects connection to client (version mismatch).

	//Game installation commands
	HDLGMAN_SERVER_PREP_GAME_INST	=	0x10,	//Creates a new partition and will also automatically invoke HDLGMAN_SERVER_INIT_GAME_WRITE.
	HDLGMAN_SERVER_INIT_GAME_WRITE,			//Opens an existing partition for writing.
	HDLGMAN_SERVER_INIT_GAME_READ,			//Opens an existing partition for reading.
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

	//OSD-resource management
	HDLGMAN_SERVER_INIT_OSD_RESOURCES	=	0x20,
	HDLGMAN_SERVER_OSD_RES_LOAD_INIT,
	HDLGMAN_SERVER_OSD_RES_LOAD,
	HDLGMAN_SERVER_WRITE_OSD_RESOURCES,
	HDLGMAN_SERVER_OSD_RES_WRITE_CANCEL,
	HDLGMAN_SERVER_GET_OSD_RES_STAT,
	HDLGMAN_SERVER_OSD_RES_READ,
	HDLGMAN_SERVER_OSD_RES_READ_TITLES,
	HDLGMAN_SERVER_INIT_DEFAULT_OSD_RESOURCES,	//Same as HDLGMAN_SERVER_INIT_OSD_RESOURCES, but doesn't require the OSD titles and is designed for subsequent pre-built OSD resource file uploading (with HDLGMAN_SERVER_OSD_RES_LOAD) from the client.
	HDLGMAN_SERVER_OSD_MC_SAVE_CHECK,
	HDLGMAN_SERVER_OSD_MC_GET_RES_STAT,
	HDLGMAN_SERVER_OSD_MC_RES_READ,

	HDLGMAN_SERVER_SHUTDOWN	=	0xFF,
};

//Must be large enough to support sending & receiving all parameters and be a multiple of 64. Only game reading/writing will use external buffers.
#define HDLGMAN_RECV_MAX		(128*1024)
#define HDLGMAN_SEND_MAX		(128*1024)

/* Client status bits */
#define CLIENT_STATUS_VERSION_VERIFIED	0x01
#define CLIENT_STATUS_OPENED_FILE	0x02
#define CLIENT_STATUS_MOUNTED_FS	0x04
#define CLIENT_STATUS_CONNECTED		0x80

/* Server status bits */
#define SERVER_STATUS_THREAD_RUN	0x80

/* Function prototypes. */
int InitializeStartServer(void);
void DeinitializeServer(void);		//Shut down the server.
