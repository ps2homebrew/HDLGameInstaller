//#define DEBUG_MODE /* Define this to enable verbose debugging messages */
//#define UI_TEST_MODE    //Define this to enable UI testing mode (Will simulate a server connection, but no connection will take place).

// Extended error codes.
#define EEXTCONNLOST 0x2000
#define EEXTABORT    0x4000

#ifdef DEBUG_MODE
#define DEBUG_PRINTF printf
#else
#define DEBUG_PRINTF(...)
#endif

#define IO_BUFFER_SIZE  IO_BANKSIZE
#define RETRY_COUNT     3 // Maximum number of attempts to make, for failures to read/write to the server.
#define RECONNECT_COUNT 5 /*  Maximum number of reconnection attempts to make, for every failed attempt to read/write. \
                              Note that the maximum number of connection attempts would be equal to RETRY_COUNT*RECONNECT_COUNT. */

/* SCE disc types */
#define SCECdPSCD    0x10
#define SCECdPSCDDA  0x11
#define SCECdPS2CD   0x12
#define SCECdPS2CDDA 0x13
#define SCECdPS2DVD  0x14

struct HDLGameEntry
{
    char PartName[33];
    wchar_t GameTitle[GAME_TITLE_MAX_LEN + 1];
    char DiscID[11];
    u8 CompatibilityModeFlags;
    u8 TRType;
    u8 TRMode;
    u8 DiscType;
    u32 sectors; // In 2048-byte units
};

struct OSD_Titles
{
    wchar_t title1[OSD_TITLE_MAX_LEN + 1];
    wchar_t title2[OSD_TITLE_MAX_LEN + 1];
};
