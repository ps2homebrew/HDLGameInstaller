typedef int iconIVECTOR[4];
typedef float iconFVECTOR[4];

typedef struct
{
    unsigned char  head[4];     // header = "PS2D"
    unsigned short type;        // filetype, used to be "unknown1" (see MCICON_TYPE_* above)
    unsigned short nlOffset;    // new line pos within title name
    unsigned unknown2;          // unknown
    unsigned trans;             // transparency
    iconIVECTOR bgCol[4];       // background color for each of the four points
    iconFVECTOR lightDir[3];    // directions of three light sources
    iconFVECTOR lightCol[3];    // colors of each of these sources
    iconFVECTOR lightAmbient;   // ambient light
    unsigned short title[34];   // application title - NOTE: stored in sjis, NOT normal ascii
    unsigned char view[64];     // list icon filename
    unsigned char copy[64];     // copy icon filename
    unsigned char del[64];      // delete icon filename
    unsigned char unknown3[512];// unknown
} mcIcon;

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
	wchar_t uninstallmes0[61];
	wchar_t uninstallmes1[61];
	wchar_t uninstallmes2[61];
};

struct ConvertedMcIcon{
	unsigned int HDDIconSysSize;
	char *HDDIconSys;
	unsigned int ListViewIconSize;
	void *ListViewIcon;
	unsigned int DeleteIconSize;
	void *DeleteIcon;
};

int LoadIconSysFile(const unsigned char *buffer, int size, struct IconSysData *data);
int GenerateHDDIconSysFile(const struct IconSysData *data, char *HDDIconSys, unsigned int OutputBufferLength);
int GenerateHDDIconSysFileFromMCSave(const mcIcon* McIconSys, char *HDDIconSys, unsigned int OutputBufferLength, const wchar_t *title1, const wchar_t *title2);
int LoadMcSaveSysFromPath(const wchar_t* SaveFilePath, mcIcon* McSaveIconSys);
int VerifyMcSave(const wchar_t *SaveFolderPath);
int ConvertMcSave(const wchar_t *SaveFolderPath, struct ConvertedMcIcon *ConvertedIcon, const wchar_t *OSDTitleLine1, const wchar_t *OSDTitleLine2);
void FreeConvertedMcSave(struct ConvertedMcIcon *ConvertedIcon);
int InstallGameInstallationOSDResources(const char *partition, const char *DiscID, const struct GameSettings *GameSettings, const struct ConvertedMcIcon *ConvertedIcon);
int UpdateGameInstallationOSDResources(const char *partition, const wchar_t *title1, const wchar_t *title2);
