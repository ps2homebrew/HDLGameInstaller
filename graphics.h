//Define if the background should be centered instead of being stretched.
//#define CENTRE_BACKGROUND	1

#define GS_WHITE GS_SETREG_RGBAQ(0xFF,0xFF,0xFF,0x00,0x00)
#define GS_BLACK GS_SETREG_RGBAQ(0x00,0x00,0x00,0x00,0x00)
#define GS_GREY GS_SETREG_RGBAQ(0x30,0x30,0x30,0x00,0x00)
#define GS_LGREY GS_SETREG_RGBAQ(0x50,0x50,0x50,0x00,0x00)
#define GS_DBLUE GS_SETREG_RGBAQ(0x00,0x00,0x50,0x00,0x00)
#define GS_BLUE GS_SETREG_RGBAQ(0x00,0x00,0x80,0x00,0x00)
#define GS_LBLUE_TRANS GS_SETREG_RGBAQ(0x00,0x00,0x80,0x30,0x00)
#define GS_YELLOW GS_SETREG_RGBAQ(0x80,0x80,0x30,0x00,0x00)
#define GS_LRED_TRANS GS_SETREG_RGBAQ(0x80,0x00,0x00,0x30,0x00)

#define GS_WHITE_FONT GS_SETREG_RGBAQ(0x80,0x80,0x80,0x80,0x00)
#define GS_GREY_FONT GS_SETREG_RGBAQ(0x30,0x30,0x30,0x80,0x00)
#define GS_YELLOW_FONT GS_SETREG_RGBAQ(0x80,0x80,0x30,0x80,0x00)
#define GS_BLUE_FONT GS_SETREG_RGBAQ(0x30,0x30,0x80,0x80,0x00)

/* Button types, for use with DrawButtonLegend() */
enum BUTTON_TYPE{
	BUTTON_TYPE_CIRCLE=0,
	BUTTON_TYPE_CROSS,
	BUTTON_TYPE_SQUARE,
	BUTTON_TYPE_TRIANGLE,
	BUTTON_TYPE_L1,
	BUTTON_TYPE_R1,
	BUTTON_TYPE_L2,
	BUTTON_TYPE_R2,
	BUTTON_TYPE_L3,
	BUTTON_TYPE_R3,
	BUTTON_TYPE_START,
	BUTTON_TYPE_SELECT,
	BUTTON_TYPE_RSTICK,
	BUTTON_TYPE_UP_RSTICK,
	BUTTON_TYPE_DOWN_RSTICK,
	BUTTON_TYPE_LEFT_RSTICK,
	BUTTON_TYPE_RIGHT_RSTICK,
	BUTTON_TYPE_LSTICK,
	BUTTON_TYPE_UP_LSTICK,
	BUTTON_TYPE_DOWN_LSTICK,
	BUTTON_TYPE_LEFT_LSTICK,
	BUTTON_TYPE_RIGHT_LSTICK,
	BUTTON_TYPE_DPAD,
	BUTTON_TYPE_LR_DPAD,
	BUTTON_TYPE_UD_DPAD,
	BUTTON_TYPE_UP_DPAD,
	BUTTON_TYPE_DOWN_DPAD,
	BUTTON_TYPE_LEFT_DPAD,
	BUTTON_TYPE_RIGHT_DPAD,

	BUTTON_TYPE_COUNT
};

enum DEVICE_TYPE{
	DEVICE_TYPE_FOLDER,
	DEVICE_TYPE_FILE,
	DEVICE_TYPE_DISK,
	DEVICE_TYPE_USB_DISK,
	DEVICE_TYPE_ROM,
	DEVICE_TYPE_SD_CARD,
	DEVICE_TYPE_DISC,

	DEVICE_TYPE_COUNT
};

#define DEVICE_ICON_SCALE	2

//Special button types
#define BUTTON_TYPE_SYS_SELECT	0x40
#define	BUTTON_TYPE_SYS_CANCEL	0x41

int UploadTexture(GSGLOBAL *gsGlobal, GSTEXTURE* Texture);
int LoadBackground(GSGLOBAL *gsGlobal, GSTEXTURE* Texture);
int LoadPadGraphics(GSGLOBAL *gsGlobal, GSTEXTURE* Texture);
int LoadDeviceIcons(GSGLOBAL *gsGlobal, GSTEXTURE* Texture);

void DrawBackground(GSGLOBAL *gsGlobal, GSTEXTURE *background);
void DrawButtonLegendWithFeedback(GSGLOBAL *gsGlobal, GSTEXTURE* PadGraphicsTexture, unsigned char ButtonType, short int x, short int y, short int z, short int *xRel);
void DrawButtonLegend(GSGLOBAL *gsGlobal, GSTEXTURE* PadGraphicsTexture, unsigned char ButtonType, short int x, short int y, short int z);
void DrawDeviceIconWithFeedback(GSGLOBAL *gsGlobal, GSTEXTURE* IconTexture, unsigned char icon, short int x, short int y, short int z, short int *xRel);
void DrawDeviceIcon(GSGLOBAL *gsGlobal, GSTEXTURE* IconTexture, unsigned char icon, short int x, short int y, short int z);
void DrawProgressBar(GSGLOBAL *gsGlobal, float percentage, short int x, short int y, short int z, short int len, u64 colour);

void SyncFlipFB(void);
