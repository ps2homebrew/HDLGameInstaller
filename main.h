//#define DEBUG_TTY_FEEDBACK /* Comment out to disable debugging messages */

//Extended error codes.
#define EEXTCONNLOST	0x2000

#define FILEIO_BLOCK_SIZE		(256*2048)			//Size of the FILEIO Read/Write buffer.

#define GAME_TITLE_MAX_LEN_BYTES	160				//In bytes, when in UTF-8 characters.
#define GAME_TITLE_MAX_LEN		(GAME_TITLE_MAX_LEN_BYTES/2)	/*	In characters. Note: the original format for HDLoader just has a 160-character space.
										But UTF-8 characters may have 1 or more bytes each. Hence this is an approximation.	*/
#define OSD_TITLE_MAX_LEN_BYTES		(OSD_TITLE_MAX_LEN*4)		//In bytes, when in UTF-8 characters
#define OSD_TITLE_MAX_LEN		16				//In characters

#ifdef DEBUG_TTY_FEEDBACK
	#define DEBUG_PRINTF(args...) printf(args)
#else
	#define DEBUG_PRINTF(args...)
#endif

#define HDLGAME_INSTALLER_VERSION	"0.821"

#define ENABLE_NETWORK_SUPPORT
//#define UI_TEST_MODE

#ifdef DEBUG_TTY_FEEDBACK
#ifdef ENABLE_NETWORK_SUPPORT
#error When DEBUG_TTY_FEEDBACK is defined, do not define ENABLE_NETWORK_SUPPORT
#endif
#endif

struct RuntimeData{
	sceCdRMode ReadMode;
	char BootDeviceID;
	volatile unsigned char IsRemoteClientConnected;
	int InstallationLockSema;	//For game I/O (Installation and reading), to protect access to the hdl0: device. Updating and deleting games doesn't require locking since the hdl0: device won't be accessed then.
	unsigned char ip_address[4], subnet_mask[4], gateway[4];
	unsigned char UseDHCP, SortTitles, EthernetFlowControl, AdvancedNetworkSettings;
	short int EthernetLinkMode;
};
