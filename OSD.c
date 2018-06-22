#include <errno.h>
#include <iopheap.h>
#include <kernel.h>
#include <libcdvd.h>
#include <libpad.h>
#include <libmc.h>
#include <loadfile.h>
#include <malloc.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>
#include <fileio.h>
#include <wchar.h>

#include <fileXio_rpc.h>
#include <sys/fcntl.h>

#include <gsKit.h>

#include "main.h"
#include "graphics.h"
#include "UI.h"
#include "pad.h"

#include "HDLGameList.h"
#include "OSD.h"

#include "sjis.h"

#define OSD_FILES_HAVE_ICON_SYS		0x01
#define OSD_FILES_HAVE_VIEW_ICON	0x02
#define OSD_FILES_HAVE_DEL_ICON		0x04

extern unsigned char SJIS2Unicode_bin[];
extern unsigned int size_SJIS2Unicode_bin;

extern unsigned char SYSTEM_cnf[];
extern unsigned int size_SYSTEM_cnf;

extern unsigned char ICON_ico[];
extern unsigned int size_ICON_ico;

extern unsigned char BOOT_kelf[];
extern unsigned int size_BOOT_kelf;

static struct OSDResourceFileEntry DefaultFileList[NUM_OSD_FILES_ENTS]={
	{
		"SYSTEM.CNF",
		NULL,
		0,
		0
	},

	{
		"ICON.SYS",
		NULL,
		0,
		FILE_FLAGS_OPTIONAL
	},

	{
		"ICON.ICO",
		NULL,
		0,
		0
	},

	{
		"DELICON.ICO",
		NULL,
		0,
		FILE_FLAGS_OPTIONAL
	},

	{
		"BOOT.KELF",
		NULL,
		0,
		FILE_FLAGS_OPTIONAL
	}
};

/* Function prototypes */
static int FindGameSaveFolder(int port, char *GameSaveFolder, unsigned int size, const char *DiscID);
static int ParseIconSysFile(const wchar_t *file, unsigned int length, struct IconSysData *data);

void InitOSDResourceFiles(void){
	SetSJISToUnicodeLookupTable(SJIS2Unicode_bin, size_SJIS2Unicode_bin);

	DefaultFileList[OSD_SYSTEM_CNF_INDEX].buffer=SYSTEM_cnf;
	DefaultFileList[OSD_SYSTEM_CNF_INDEX].size=size_SYSTEM_cnf;
	DefaultFileList[OSD_VIEW_ICON_INDEX].buffer=ICON_ico;
	DefaultFileList[OSD_VIEW_ICON_INDEX].size=size_ICON_ico;
	DefaultFileList[OSD_BOOT_KELF_INDEX].buffer=BOOT_kelf;
	DefaultFileList[OSD_BOOT_KELF_INDEX].size=size_BOOT_kelf;
}

int LoadIconSysFile(const unsigned char *buffer, int size, struct IconSysData *data)
{
	int result, ConvertedSize;
	char *iconSysNullTerminated;
	wchar_t *ConvertedStringBuffer;

	if((iconSysNullTerminated = malloc(size + 1)) != NULL)
	{
		memcpy(iconSysNullTerminated, buffer, size);
		iconSysNullTerminated[size] = '\0';

		//At this point, the exact number of wchars is not known. Hence allocate one wchar for each byte that could be potentially converted.
		if((ConvertedStringBuffer=malloc(size*sizeof(wchar_t)))!=NULL)
		{
			ConvertedSize=mbstowcs(ConvertedStringBuffer, iconSysNullTerminated, size);
			free(iconSysNullTerminated);

			result=ParseIconSysFile(ConvertedStringBuffer, ConvertedSize, data);
			free(ConvertedStringBuffer);
		}
		else {
			free(iconSysNullTerminated);
			result=-ENOMEM;
		}
	}
	else
		result = -ENOMEM;

	return result;
}

static int IsConfigFileLineParam(const wchar_t *line, const char *ParamName){
	unsigned int i;

	for(i=0; ParamName[i]!='\0'; i++){
		if(line[i]!=ParamName[i]){
			break;
		}
	}

	return(ParamName[i]=='\0' && !iswalpha(line[i]));
}

static wchar_t *ConfigLineTrimLeadingWhitespaces(wchar_t *line){
	//Trim whitespaces
	while(*line==' ') line++;

	return line;
}

static wchar_t *GetConfigFileLineParamValue(wchar_t *line){
	wchar_t *StartOfValue;

	if((StartOfValue=wcschr(line, '='))!=NULL){
		StartOfValue=ConfigLineTrimLeadingWhitespaces(StartOfValue+1);
	}

	return StartOfValue;
}

static int GetStringValueFromConfigLine(wchar_t *field, unsigned int count, wchar_t *line){
	wchar_t *value;
	int result;

	if((value=GetConfigFileLineParamValue(line))!=NULL){
		wcsncpy(field, value, count);
		field[count]='\0';
		result=0;
	}
	else result=-1;

	return result;
}

static int GetXYZFVectorValueFromConfigLine(float *field, wchar_t *line){
	wchar_t *value;
	int ValueLength, result;

	if((value=GetConfigFileLineParamValue(line))!=NULL && ((ValueLength=wcslen(value))>0)){
		field[0]=(float)wcstod(value, &value);
		if(value!=NULL && (value=ConfigLineTrimLeadingWhitespaces(value))[0]==','){
			field[1]=(float)wcstod(ConfigLineTrimLeadingWhitespaces(value+1), &value);
			if(value!=NULL && (value=ConfigLineTrimLeadingWhitespaces(value))[0]==','){
				field[2]=(float)wcstod(ConfigLineTrimLeadingWhitespaces(value+1), &value);
				result=0;
			}
			else result=-1;
		}
		else result=-1;
	}
	else result=-1;

	return result;
}

static int GetXYZIVectorValueFromConfigLine(unsigned char *field, wchar_t *line){
	wchar_t *value;
	int ValueLength, result;

	if((value=GetConfigFileLineParamValue(line))!=NULL && ((ValueLength=wcslen(value))>0)){
		field[0]=(unsigned char)wcstoul(value, &value, 10);
		if(value!=NULL && (value=ConfigLineTrimLeadingWhitespaces(value))[0]==','){
			field[1]=(unsigned char)wcstoul(ConfigLineTrimLeadingWhitespaces(value+1), &value, 10);
			if(value!=NULL && (value=ConfigLineTrimLeadingWhitespaces(value))[0]==','){
				field[2]=(unsigned char)wcstoul(ConfigLineTrimLeadingWhitespaces(value+1), &value, 10);
				result=0;
			}
			else result=-1;
		}
		else result=-1;
	}
	else result=-1;

	return result;
}

static int ParseIconSysFile(const wchar_t *file, unsigned int length, struct IconSysData *data){
	int result, ValueLength;
	wchar_t *line, *value, *buffer;

	memset(data, 0, sizeof(struct IconSysData));

	if((buffer=malloc((length+1)*sizeof(wchar_t)))!=NULL){
		wcsncpy(buffer, file, length);
		buffer[length]='\0';

		line=wcstok(buffer, L"\r\n");
		if(line!=NULL && wcscmp(line, L"PS2X")==0){
			result=0;
			while((line=wcstok(NULL, L"\r\n"))!=NULL){
				if(IsConfigFileLineParam(line, "title0")){
					result=GetStringValueFromConfigLine(data->title0, OSD_TITLE_MAX_LEN+1, line);
				}
				else if(IsConfigFileLineParam(line, "title1")){
					result=GetStringValueFromConfigLine(data->title1, OSD_TITLE_MAX_LEN+1, line);
				}
				else if(IsConfigFileLineParam(line, "bgcola")){
					if((value=GetConfigFileLineParamValue(line))!=NULL && ((ValueLength=wcslen(value))>0)){
						data->bgcola=(unsigned char)wcstoul(value, NULL, 10);
						result=0;
					}
					else result=-1;
				}
				else if(IsConfigFileLineParam(line, "bgcol0")){
					result=GetXYZIVectorValueFromConfigLine(data->bgcol0, line);
				}
				else if(IsConfigFileLineParam(line, "bgcol1")){
					result=GetXYZIVectorValueFromConfigLine(data->bgcol1, line);
				}
				else if(IsConfigFileLineParam(line, "bgcol2")){
					result=GetXYZIVectorValueFromConfigLine(data->bgcol2, line);
				}
				else if(IsConfigFileLineParam(line, "bgcol3")){
					result=GetXYZIVectorValueFromConfigLine(data->bgcol3, line);
				}
				else if(IsConfigFileLineParam(line, "lightdir0")){
					result=GetXYZFVectorValueFromConfigLine(data->lightdir0, line);
				}
				else if(IsConfigFileLineParam(line, "lightdir1")){
					result=GetXYZFVectorValueFromConfigLine(data->lightdir1, line);
				}
				else if(IsConfigFileLineParam(line, "lightdir2")){
					result=GetXYZFVectorValueFromConfigLine(data->lightdir2, line);
				}
				else if(IsConfigFileLineParam(line, "lightcolamb")){
					result=GetXYZIVectorValueFromConfigLine(data->lightcolamb, line);
				}
				else if(IsConfigFileLineParam(line, "lightcol0")){
					result=GetXYZIVectorValueFromConfigLine(data->lightcol0, line);
				}
				else if(IsConfigFileLineParam(line, "lightcol1")){
					result=GetXYZIVectorValueFromConfigLine(data->lightcol1, line);
				}
				else if(IsConfigFileLineParam(line, "lightcol2")){
					result=GetXYZIVectorValueFromConfigLine(data->lightcol2, line);
				}
				else if(IsConfigFileLineParam(line, "uninstallmes0")){
					result=GetStringValueFromConfigLine(data->uninstallmes0, sizeof(data->uninstallmes0), line);
				}
				else if(IsConfigFileLineParam(line, "uninstallmes1")){
					result=GetStringValueFromConfigLine(data->uninstallmes1, sizeof(data->uninstallmes1), line);
				}
				else if(IsConfigFileLineParam(line, "uninstallmes2")){
					result=GetStringValueFromConfigLine(data->uninstallmes2, sizeof(data->uninstallmes2), line);
				}
				else{
					DEBUG_PRINTF("ParseIconSysFile: Unrecognized line: %s\n", line);
					result=-1;
				}

				if(result!=0){
					DEBUG_PRINTF("ParseIconSysFile: Parse error. line: %s\n", line);
					break;
				}
			}
		}
		else{
			DEBUG_PRINTF("ParseIconSysFile: Not an icon file: %s\n", line);
			result=-EINVAL;
		}

		free(buffer);
	}
	else result=-ENOMEM;

	return result;
}

static int GenerateHDDIconSysFile(const struct IconSysData *data, char *HDDIconSys){
	wchar_t buffer[512];	//Probably 5+26+26+16+24+24+24+24+32+32+32+24+24+24+24+64+64+64=512 would be enough.

	swprintf(buffer, sizeof(buffer)/sizeof(wchar_t),
			L"PS2X\n"
			"title0 = %s\n"
			"title1 = %s\n"
			"bgcola = %u\n"
			"bgcol0 = %u,%u,%u\n"
			"bgcol1 = %u,%u,%u\n"
			"bgcol2 = %u,%u,%u\n"
			"bgcol3 = %u,%u,%u\n"
			"lightdir0 = %1.4f,%1.4f,%1.4f\n"
			"lightdir1 = %1.4f,%1.4f,%1.4f\n"
			"lightdir2 = %1.4f,%1.4f,%1.4f\n"
			"lightcolamb = %u,%u,%u\n"
			"lightcol0 = %u,%u,%u\n"
			"lightcol1 = %u,%u,%u\n"
			"lightcol2 = %u,%u,%u\n"
			"uninstallmes0 = %s\n"
			"uninstallmes1 = %s\n"
			"uninstallmes2 = %s\n",
			data->title0,	//Title line 1 is mandatory.
			data->title1,
			data->bgcola,
			data->bgcol0[0], data->bgcol0[1], data->bgcol0[2],
			data->bgcol1[0], data->bgcol1[1], data->bgcol1[2],
			data->bgcol2[0], data->bgcol2[1], data->bgcol2[2],
			data->bgcol3[0], data->bgcol3[1], data->bgcol3[2],
			data->lightdir0[0], data->lightdir0[1], data->lightdir0[2],
			data->lightdir1[0], data->lightdir1[1], data->lightdir1[2],
			data->lightdir2[0], data->lightdir2[1], data->lightdir2[2],
			data->lightcolamb[0], data->lightcolamb[1], data->lightcolamb[2],
			data->lightcol0[0], data->lightcol0[1], data->lightcol0[2],
			data->lightcol1[0], data->lightcol1[1], data->lightcol1[2],
			data->lightcol2[0], data->lightcol2[1], data->lightcol2[2],
			data->uninstallmes0, data->uninstallmes1, data->uninstallmes2
		);

	return(wcstombs(HDDIconSys, buffer, 512) + 1);
}

static int IsASCIITitle(const wchar_t *string){
	wchar_t character;
	int i, result;

	//Full-width ASCII characters (ASCII codes 0x21 to 0x7E): 0xFF01 to 0xFF5E.
	result=1;
	for(i=0; (character=*string)!='\0'; i++,string++)
	{
		if((!iswalnum(character) && !iswspace(character)) && (!(character>=0xFF01 && character<=0xFF5E) && character!=0x3000))
		{
			result=0;
			break;
		}
	}

	return result;
}

static void ConvertSJIS_ASCII_to_ASCII(wchar_t *out, const wchar_t *in, int length)
{
	wchar_t character;
	int i;

	//Full-width characters (for ASCII codes 0x21 to 0x7E): 0xFF01 to 0xFF5E.
	i=0;
	for(i=0; (character=*in)!='\0'; i++,out++,in++)
	{
		if(i >= length) break;

		if(character>=0xFF01 && character<=0xFF5E)
		{
			character=character-0xFF01+0x21;
		}
		else if(character==0x3000)
		{
			character=' ';
		}

		*out=character;
	}
	*out='\0';
}

void ConvertMcTitle(const mcIcon *icon, wchar_t *title1, wchar_t *title2)
{
	wchar_t SaveDataTitle[33];
	wchar_t title1Raw[OSD_TITLE_MAX_LEN+1], title2Raw[OSD_TITLE_MAX_LEN+1];
	int len;

	if(icon->nlOffset>0 && icon->nlOffset<sizeof(icon->title))
	{	//The title is split with a newline.
		len = SJISToUnicode((const char*)icon->title, icon->nlOffset, title1Raw, OSD_TITLE_MAX_LEN-1);
		title1Raw[len]='\0';
		SJISToUnicode(&((const char*)icon->title)[icon->nlOffset], sizeof(icon->title)-icon->nlOffset, title2Raw, OSD_TITLE_MAX_LEN-1);
		title1Raw[len]='\0';

		if(IsASCIITitle(title1Raw) && (title2Raw==NULL || IsASCIITitle(title2Raw)))
		{	//Convert the title to ASCII, if the title can be represented in ASCII.
			ConvertSJIS_ASCII_to_ASCII(title1, title1Raw, OSD_TITLE_MAX_LEN+1);
			ConvertSJIS_ASCII_to_ASCII(title2, title2Raw, OSD_TITLE_MAX_LEN+1);
		}
		else
		{	//Otherwise, just copy the title.
			wcsncpy(title1, title1Raw, OSD_TITLE_MAX_LEN);
			title1[OSD_TITLE_MAX_LEN]='\0';
			wcsncpy(title2, title2Raw, OSD_TITLE_MAX_LEN);
			title2[OSD_TITLE_MAX_LEN]='\0';
		}
	}
	else
	{	//Title has no newline.
		len = SJISToUnicode((const char*)icon->title, sizeof(icon->title), SaveDataTitle, sizeof(SaveDataTitle)/sizeof(wchar_t)-1);
		title1Raw[len]='\0';

		if(IsASCIITitle(SaveDataTitle))
		{	//Convert the title to ASCII, if the title can be represented in ASCII.
			ConvertSJIS_ASCII_to_ASCII(title1, SaveDataTitle, OSD_TITLE_MAX_LEN+1);
			ConvertSJIS_ASCII_to_ASCII(title2, &SaveDataTitle[OSD_TITLE_MAX_LEN], OSD_TITLE_MAX_LEN+1);
		}
		else
		{	//Otherwise, just split the title into 2.
			wcsncpy(title1, SaveDataTitle, OSD_TITLE_MAX_LEN);
			title1[OSD_TITLE_MAX_LEN]='\0';
			wcsncpy(title2, &SaveDataTitle[OSD_TITLE_MAX_LEN], OSD_TITLE_MAX_LEN);
			title2[OSD_TITLE_MAX_LEN]='\0';
		}
	}
}

static int GenerateHDDIconSysFileFromMCSave(const mcIcon* McIconSys, char *HDDIconSys, const wchar_t *OSDTitleLine1, const wchar_t *OSDTitleLine2){
	struct IconSysData IconSysData;

	wcsncpy(IconSysData.title0, OSDTitleLine1, OSD_TITLE_MAX_LEN);
	IconSysData.title0[OSD_TITLE_MAX_LEN]='\0';

	//Line 2 is optional
	if(OSDTitleLine2!=NULL){
		wcsncpy(IconSysData.title1, OSDTitleLine2, OSD_TITLE_MAX_LEN);
		IconSysData.title1[OSD_TITLE_MAX_LEN]='\0';
	}
	else IconSysData.title1[0]='\0';

	IconSysData.bgcola=McIconSys->trans;
	IconSysData.bgcol0[0]=McIconSys->bgCol[0][0]/2;
	IconSysData.bgcol0[1]=McIconSys->bgCol[0][1]/2;
	IconSysData.bgcol0[2]=McIconSys->bgCol[0][2]/2;
	IconSysData.bgcol1[0]=McIconSys->bgCol[1][0]/2;
	IconSysData.bgcol1[1]=McIconSys->bgCol[1][1]/2;
	IconSysData.bgcol1[2]=McIconSys->bgCol[1][2]/2;
	IconSysData.bgcol2[0]=McIconSys->bgCol[2][0]/2;
	IconSysData.bgcol2[1]=McIconSys->bgCol[2][1]/2;
	IconSysData.bgcol2[2]=McIconSys->bgCol[2][2]/2;
	IconSysData.bgcol3[0]=McIconSys->bgCol[3][0]/2;
	IconSysData.bgcol3[1]=McIconSys->bgCol[3][1]/2;
	IconSysData.bgcol3[2]=McIconSys->bgCol[3][2]/2;
	IconSysData.lightdir0[0]=McIconSys->lightDir[0][0];
	IconSysData.lightdir0[1]=McIconSys->lightDir[0][1];
	IconSysData.lightdir0[2]=McIconSys->lightDir[0][2];
	IconSysData.lightdir1[0]=McIconSys->lightDir[1][0];
	IconSysData.lightdir1[1]=McIconSys->lightDir[1][1];
	IconSysData.lightdir1[2]=McIconSys->lightDir[1][2];
	IconSysData.lightdir2[0]=McIconSys->lightDir[2][0];
	IconSysData.lightdir2[1]=McIconSys->lightDir[2][1];
	IconSysData.lightdir2[2]=McIconSys->lightDir[2][2];
	IconSysData.lightcolamb[0]=(unsigned char)(McIconSys->lightAmbient[0]*128);
	IconSysData.lightcolamb[1]=(unsigned char)(McIconSys->lightAmbient[1]*128);
	IconSysData.lightcolamb[2]=(unsigned char)(McIconSys->lightAmbient[2]*128);
	IconSysData.lightcol0[0]=(unsigned char)(McIconSys->lightCol[0][0]*128);
	IconSysData.lightcol0[1]=(unsigned char)(McIconSys->lightCol[0][1]*128);
	IconSysData.lightcol0[2]=(unsigned char)(McIconSys->lightCol[0][2]*128);
	IconSysData.lightcol1[0]=(unsigned char)(McIconSys->lightCol[1][0]*128);
	IconSysData.lightcol1[1]=(unsigned char)(McIconSys->lightCol[1][1]*128);
	IconSysData.lightcol1[2]=(unsigned char)(McIconSys->lightCol[1][2]*128);
	IconSysData.lightcol2[0]=(unsigned char)(McIconSys->lightCol[2][0]*128);
	IconSysData.lightcol2[1]=(unsigned char)(McIconSys->lightCol[2][1]*128);
	IconSysData.lightcol2[2]=(unsigned char)(McIconSys->lightCol[2][2]*128);
	wcscpy(IconSysData.uninstallmes0, L"This will delete the game.");
	IconSysData.uninstallmes1[0]='\0';
	IconSysData.uninstallmes2[0]='\0';

	DEBUG_PRINTF("MC Save title: %s. New title: %s,%s\n", (char *)McIconSys->title, OSDTitleLine1, OSDTitleLine2);
	return GenerateHDDIconSysFile(&IconSysData, HDDIconSys);
}

int CheckExistingMcSave(const char* DiscID){
	int result, IconSysFD;
	char McPath[70], SaveFolderPath[38];
	unsigned int i;

	for(i=0,result=-ENOENT; i<2; i++){
		if((result=FindGameSaveFolder(i, SaveFolderPath, sizeof(SaveFolderPath), DiscID))>=0){
			sprintf(McPath, "%s/icon.sys", SaveFolderPath);
			if((IconSysFD=fileXioOpen(McPath, O_RDONLY))>=0){
				fileXioClose(IconSysFD);
				result=0;
				break;
			}
		}
	}

	return result;
}

int GetExistingFileInMcSaveStat(const char *DiscID, const char *filename, iox_stat_t *stat){
	int result, IconSysFD;
	char McPath[70], SaveFolderPath[38];
	unsigned int i;

	for(i=0,result=-ENOENT; i<2; i++){
		if(FindGameSaveFolder(i, SaveFolderPath, sizeof(SaveFolderPath), DiscID)>=0){
			sprintf(McPath, "%s/%s", SaveFolderPath, filename);
			if((IconSysFD=fileXioOpen(McPath, O_RDONLY))>=0){
				fileXioClose(IconSysFD);
				result=fileXioGetStat(McPath, stat);
				break;
			}
		}
	}

	return result;
}

int ReadExistingFileInMcSave(const char *DiscID, const char *filename, void *buffer, unsigned int length){
	int result, IconSysFD;
	char McPath[70], SaveFolderPath[38];
	unsigned int i;

	for(i=0,result=-ENOENT; i<2; i++){
		if(FindGameSaveFolder(i, SaveFolderPath, sizeof(SaveFolderPath), DiscID)>=0){
			sprintf(McPath, "%s/%s", SaveFolderPath, filename);
			if((IconSysFD=fileXioOpen(McPath, O_RDONLY))>=0){
				result=(fileXioRead(IconSysFD, buffer, length)!=length)?-ENOENT:0;
				fileXioClose(IconSysFD);
				break;
			}
		}
	}

	return result;
}

int LoadMcSaveSysFromPath(const char* SaveFolderPath, mcIcon* McSaveIconSys){
	int result, IconSysFD;
	char McPath[70];

	sprintf(McPath, "%s/icon.sys", SaveFolderPath);
	if((IconSysFD=fileXioOpen(McPath, O_RDONLY))>=0){
		result=fileXioRead(IconSysFD, McSaveIconSys, sizeof(mcIcon));
		fileXioClose(IconSysFD);

		if(result==sizeof(mcIcon)){
			result=0;
		}
		else result=-EIO;
	}
	else{
		result=IconSysFD;
		DEBUG_PRINTF("Memory card save file not found: %d\n", result);
	}

	return result;
}

int LoadMcSaveSys(char* SaveFolderPath, mcIcon* McSaveIconSys, const char* DiscID){
	int result;
	unsigned int i;

	for(i=0,result=-ENOENT; i<2; i++){
		if((result=FindGameSaveFolder(i, SaveFolderPath, 38, DiscID))>=0){
			if((result=LoadMcSaveSysFromPath(SaveFolderPath, McSaveIconSys))==0) break;
		}
		else DEBUG_PRINTF("Memory card save folder not found: %d\n", result);
	}

	return result;
}

static int GenerateIconFilesFromMCSave(const char* SaveFolderPath, const mcIcon* McSaveIconSys, struct OSDResourceFileEntry *InstFileList, const wchar_t *OSDTitleLine1, const wchar_t *OSDTitleLine2){
	int result, IconSysFD;
	char McPath[70];
	unsigned int length;
	void *buffer, *buffer2;

	DEBUG_PRINTF("Converting MC icon...\n");
	result=OSD_FILES_HAVE_ICON_SYS;
	/* Convert the MC icon file into the HDD OSD format. */
	buffer=malloc(640);	/* Allocate sufficient memory to accommodate the longest possible title and the standard icon.sys file fields as in the template. */
	buffer2=memalign(64, length=GenerateHDDIconSysFileFromMCSave(McSaveIconSys, buffer, OSDTitleLine1, OSDTitleLine2)-1);
	memcpy(buffer2, buffer, length);
	free(buffer);

	LoadOSDResource(OSD_ICON_SYS_INDEX, InstFileList, buffer2, length, 1);

	DEBUG_PRINTF("Reading in MC list view icon...\n");

	sprintf(McPath, "%s/%s", SaveFolderPath, McSaveIconSys->view);
	if((IconSysFD=fileXioOpen(McPath, O_RDONLY))>=0){
		result|=OSD_FILES_HAVE_VIEW_ICON;
		length=fileXioLseek(IconSysFD, 0, SEEK_END);
		fileXioLseek(IconSysFD, 0, SEEK_SET);
		buffer=memalign(64, length);
		fileXioRead(IconSysFD, buffer, length);
		fileXioClose(IconSysFD);

		LoadOSDResource(OSD_VIEW_ICON_INDEX, InstFileList, buffer, length, 1);

		DEBUG_PRINTF("Reading in MC delete view icon...\n");

		if(strcmp(McSaveIconSys->del, McSaveIconSys->view)!=0){
			sprintf(McPath, "%s/%s", SaveFolderPath, McSaveIconSys->del);
			if((IconSysFD=open(McPath, O_RDONLY))>=0){
				result|=OSD_FILES_HAVE_DEL_ICON;
				length=fileXioLseek(IconSysFD, 0, SEEK_END);
				fileXioLseek(IconSysFD, 0, SEEK_SET);
				buffer=memalign(64, length);
				fileXioRead(IconSysFD, buffer, length);
				fileXioClose(IconSysFD);

				LoadOSDResource(OSD_DEL_ICON_INDEX, InstFileList, buffer, length, 1);
			}
		}
		else DEBUG_PRINTF("No delete icon found.\n");
	}
	else DEBUG_PRINTF("Unable to load list view icon: %d\n", IconSysFD);

	return(result&OSD_FILES_HAVE_ICON_SYS && result&OSD_FILES_HAVE_VIEW_ICON?0:-ENOENT);
}

static int FindGameSaveFolder(int port, char *GameSaveFolder, unsigned int size, const char *DiscID){
	iox_dirent_t dirent;
	int result, fd;

	/* Minimum buffer size required: 5 charcters ("mc<card number>:/") + 32 characters (Maximum name length of any file/folder on a Sony PS2 Memory Card) + 1 NULL terminator = 38 */
	if(size>=38){
		sprintf(GameSaveFolder, "mc%u:/", port);
		DEBUG_PRINTF("MC: %u\n", port);

		result=-ENOENT;
		if((fd=fileXioDopen(GameSaveFolder))>=0){
			while(fileXioDread(fd, &dirent)>0){
				DEBUG_PRINTF("%s\n", dirent.name);
				if(strstr(dirent.name, DiscID)!=NULL){
					result=0;
					strcat(GameSaveFolder, dirent.name);
					break;
				}
			}

			fileXioDclose(fd);
		}
	}
	else result=-ENOMEM;

	return result;
}

void PrepareOSDDefaultResources(struct OSDResourceFileEntry *InstFileList){
	memcpy(InstFileList, DefaultFileList, NUM_OSD_FILES_ENTS*sizeof(struct OSDResourceFileEntry));
}

int PrepareOSDResources(const char* SaveFolderPath, const mcIcon* McSaveIconSys, const wchar_t *OSDTitleLine1, const wchar_t *OSDTitleLine2, struct OSDResourceFileEntry *InstFileList){
	int McIconsLoaded, size;
	void *buffer, *buffer2;
	static const struct IconSysData DefaultGameIconSys={
		L"",
		L"",
		64,
		{22, 47, 92},
		{3, 10, 28},
		{3, 10, 28},
		{22, 47, 92},
		{0.5f, 0.5f, 0.5f},
		{0.0f, -0.4f, -0.1f},
		{-0.5f, -0.5f, 0.5f},
		{62, 62, 55},
		{33, 42, 64},
		{18, 18, 49},
		{31, 31, 31},
		L"This will delete the game.",
		L"",
		L""
	};
	struct IconSysData IconSysData;

	PrepareOSDDefaultResources(InstFileList);
	/*
		Attempt to extract the resources used by the game's savedata for use with this game installation.
			If that fails, prepare the default icon and icon System/Parameter file.
		Whatever happens, the Icon System/Parameter file (OSD_ICON_SYS_INDEX) will be in a dynamically-allocated buffer, and that shall be freed later on.

		The normal view icon (OSD_VIEW_ICON_INDEX) might or might not be in a dynamically-allocated buffer that should be freed within this function.
			If it was from an existing savedata folder, it will be in a dynamically-allocated buffer that shall be freed later on.
			Otherwise, it will point to the icon that will only be freed (automatically) when this program terminates (Hence, do not free it here!).
	*/

	if(SaveFolderPath==NULL || McSaveIconSys==NULL  || GenerateIconFilesFromMCSave(SaveFolderPath, McSaveIconSys, InstFileList, OSDTitleLine1, OSDTitleLine2)<0){
		memcpy(&IconSysData, &DefaultGameIconSys, sizeof(IconSysData));
		wcsncpy(IconSysData.title0, OSDTitleLine1, OSD_TITLE_MAX_LEN);
		IconSysData.title0[OSD_TITLE_MAX_LEN]='\0';
		wcsncpy(IconSysData.title1, OSDTitleLine2, OSD_TITLE_MAX_LEN);
		IconSysData.title1[OSD_TITLE_MAX_LEN]='\0';
		buffer=malloc(640);	/* Allocate sufficient memory to accommodate the longest possible title and the standard icon.sys file fields as in the template. */
		buffer2=memalign(64, size=GenerateHDDIconSysFile(&IconSysData, buffer)-1);
		memcpy(buffer2, buffer, size);
		free(buffer);
		LoadOSDResource(OSD_ICON_SYS_INDEX, InstFileList, buffer2, size, 1);

		McIconsLoaded=0;
	}
	else{
		McIconsLoaded=1;
	}

	return McIconsLoaded;
}

int LoadOSDResource(unsigned int index, struct OSDResourceFileEntry *InstFileList, void *buffer, unsigned int length, unsigned char IsAllocatedBuffer){
	int result;

	if(index<NUM_OSD_FILES_ENTS){
		if(InstFileList[index].buffer!=NULL && (InstFileList[index].flags&FILE_FLAGS_ALLOCATED)) free(InstFileList[index].buffer);

		InstFileList[index].buffer=buffer;
		InstFileList[index].size=length;
		InstFileList[index].flags=IsAllocatedBuffer?FILE_FLAGS_ALLOCATED:0;

		result=0;
	}
	else result=-ENXIO;

	return result;
}

void FreeOSDResources(struct OSDResourceFileEntry *InstFileList){
	unsigned int i;

	for(i=0; i<NUM_OSD_FILES_ENTS; i++){
		if(InstFileList[i].flags&FILE_FLAGS_ALLOCATED && InstFileList[i].buffer!=NULL){
			free(InstFileList[i].buffer);
			InstFileList[i].buffer=NULL;
			InstFileList[i].flags&=~FILE_FLAGS_ALLOCATED;
		}
	}
}

static inline int InstallOSDFile(int fd, unsigned int index, const struct OSDResourceFileEntry *file, t_PartAttrTab *PartAttr){
	int result, LengthRoundedUp;
	static unsigned int PartExtAttrOffset=0;
	void *buffer;

	if(index==0){
		memset(PartAttr, 0, sizeof(t_PartAttrTab));
		memcpy(PartAttr->magic, "PS2ICON3D", 9);
		PartExtAttrOffset=sizeof(t_PartAttrTab);
	}

	if(file->size<1){
		if(file->flags&FILE_FLAGS_OPTIONAL){
			if(index==OSD_DEL_ICON_INDEX){
				PartAttr->FileEnt[index].offset=PartAttr->FileEnt[OSD_VIEW_ICON_INDEX].offset;
				PartAttr->FileEnt[index].size=PartAttr->FileEnt[OSD_VIEW_ICON_INDEX].size;

				DEBUG_PRINTF("[Clone] Writing OSD file index %u offset: %u length %u\n", index, PartAttr->FileEnt[index].offset, PartAttr->FileEnt[index].size);

				result=PartAttr->FileEnt[index].offset;
			}
			else{
				DEBUG_PRINTF("Skipping resource #%d.\n", index+1);
				result=0;
			}
		}else result=-EINVAL;

		return result;
	}

	LengthRoundedUp=(file->size+0x1FF)&~0x1FF;

	result=0;
	/* As HDLoader's game format conflicts with Sony's APA specifications, we have to avoid using sectors at offset 0x800 and 0x801 as HDLoader uses them to store the game's information. :( */
	if((PartExtAttrOffset>=HDL_GAME_DATA_OFFSET && (PartExtAttrOffset<HDL_GAME_DATA_OFFSET+1024)) || (PartExtAttrOffset<=HDL_GAME_DATA_OFFSET && (PartExtAttrOffset+LengthRoundedUp>HDL_GAME_DATA_OFFSET))){
		DEBUG_PRINTF("Adjusting offset to sector offset 0x802\n");
		PartExtAttrOffset=1049600;
	}

	DEBUG_PRINTF("Writing OSD file index %u offset: %u buffer %p length %u\n", index, PartExtAttrOffset, file->buffer, file->size);

	fileXioLseek(fd, PartExtAttrOffset, SEEK_SET);

	//I/O must be done in multiples of 512-byte sectors. The buffer must also be aligned on 64-byte boundaries to avoid the I/O RPC splitting up data when it does alignment.
	if(file->size!=LengthRoundedUp || ((unsigned int)file->buffer&0x3F)!=0){
		if((buffer=memalign(64, LengthRoundedUp))!=NULL){
			memcpy(buffer, file->buffer, file->size);
			memset(&((unsigned char*)buffer)[file->size], 0, LengthRoundedUp-file->size);
			result=fileXioWrite(fd, buffer, LengthRoundedUp);
			free(buffer);
		}else result=-ENOMEM;
	}else{
		result=fileXioWrite(fd, file->buffer, file->size);
	}

	if(result==LengthRoundedUp){
		PartAttr->FileEnt[index].offset=PartExtAttrOffset;
		PartAttr->FileEnt[index].size=file->size;
		result=0;
	}else{
		DEBUG_PRINTF("\nWrite fault encountered while writing to the disk.\n");
		result=-EIO;
	}

	PartExtAttrOffset+=((file->size+0x1FF)&~0x1FF);

	return result;
}

int GetOSDResourceFileStats(const char *partition, OSDResFileEnt_t *files){
	int fd, result;
	t_PartAttrTab *PartAttrTab;

	if((PartAttrTab=memalign(64, sizeof(t_PartAttrTab)))!=NULL){
		if((fd=fileXioOpen(partition, O_RDONLY))>=0){
			if(fileXioRead(fd, PartAttrTab, sizeof(t_PartAttrTab))==sizeof(t_PartAttrTab)){
				memcpy(files, PartAttrTab->FileEnt, sizeof(PartAttrTab->FileEnt));
				result=0;
			}else result=-EIO;

			fileXioClose(fd);
		}else result=fd;

		free(PartAttrTab);
	}else result=-ENOMEM;

	return result;
}

int ReadOSDResourceFile(const char *partition, int index, const OSDResFileEnt_t *files, void *buffer){
	int result, fd;
	unsigned int LengthRounded;
	void *tempbuffer;

	if(index>=0 && index<NUM_OSD_FILES_ENTS){
		LengthRounded=(files[index].size+0x1FF)&~0x1FF;

		if((tempbuffer=memalign(64, LengthRounded))!=NULL){
			if((fd=fileXioOpen(partition, O_RDONLY))>=0){
				fileXioLseek(fd, files[index].offset, SEEK_SET);

				if(fileXioRead(fd, tempbuffer, LengthRounded)==LengthRounded){
					memcpy(buffer, tempbuffer, files[index].size);
					result=0;
				}
				else result=-EIO;

				fileXioClose(fd);
			}
			else result=fd;

			free(tempbuffer);
		}
		else result=-ENOMEM;
	}
	else result=-EINVAL;

	return result;
}

int InstallOSDFiles(const char *partition, struct OSDResourceFileEntry *InstFileList){
	int result, fd;
	unsigned int i;
	t_PartAttrTab *PartAttr;

	result=0;
	DEBUG_PRINTF("Installing OSD files...");
	DisplayFlashStatusUpdate(SYS_UI_MSG_PLEASE_WAIT);

	if((PartAttr=memalign(64, sizeof(t_PartAttrTab)))!=NULL){
		if((fd=fileXioOpen(partition, O_WRONLY, 0644))>=0){
			/* Install the OSD files. */
			for(i=0; (result>=0) && (i<NUM_OSD_FILES_ENTS); i++){
				DEBUG_PRINTF("OSD File index: %d, buffer: %p, size: %u\n", i, InstFileList[i].buffer, InstFileList[i].size);
				result=InstallOSDFile(fd, i, &InstFileList[i], PartAttr);
			}

			fileXioLseek(fd, 0, SEEK_SET);

			/* Write the File Table within the Extended Attribute area in the APA partition to the disk. */
			if(fileXioWrite(fd, PartAttr, sizeof(t_PartAttrTab))!=sizeof(t_PartAttrTab)){
				DEBUG_PRINTF("\nWrite fault encountered while writing to the extended attribute table\n");
				result=-EIO;
			}

			fileXioClose(fd);
			DEBUG_PRINTF("Done!\n");
		}else{
			DEBUG_PRINTF("\nUnable to open partition: %s. Result: %d\n", partition, fd);
			result=fd;
		}

		free(PartAttr);
	}else{
		DEBUG_PRINTF("\nUnable to allocate memory for OSD resource file installation.\n");
		result=-ENOMEM;
	}

	return result;
}

int GetGameInstallationOSDIconSys(const char *partition, struct IconSysData *IconSys){
	char path[40];
	OSDResFileEnt_t files[NUM_OSD_FILES_ENTS];
	void *IconSysBuffer;
	int result;

	memset(IconSys, 0, sizeof(struct IconSysData));
	sprintf(path, "hdd0:%s", partition);
	if((result=GetOSDResourceFileStats(path, files))==0){
		if((IconSysBuffer=memalign(64, files[OSD_ICON_SYS_INDEX].size))!=NULL){
			if((result=ReadOSDResourceFile(path, OSD_ICON_SYS_INDEX, files, IconSysBuffer))==0){
				result=LoadIconSysFile(IconSysBuffer, files[OSD_ICON_SYS_INDEX].size, IconSys);
			} else {
				DEBUG_PRINTF("UpdateGameInstallationOSDResources: Failed to read OSD icon.sys: %d\n", result);
			}

			free(IconSysBuffer);
		}
		else{
			result=-ENOMEM;
			DEBUG_PRINTF("GetGameInstallationOSDIconSys: Failed to allocate %d bytes of memory for OSD icon.sys: %d\n", files[OSD_ICON_SYS_INDEX].size, result);
		}
	}
	else{
		DEBUG_PRINTF("GetGameInstallationOSDIconSys: Can't getstat the OSD resource files: %d\n", result);
	}

	return result;
}

int InstallGameInstallationOSDResources(const char *partition, const struct GameSettings *GameSettings, const mcIcon *McSaveIconSys, const char *SaveDataPath){
	char path[38];
	struct OSDResourceFileEntry InstFileList[NUM_OSD_FILES_ENTS];
	int result;

	sprintf(path, "hdd0:%s", partition);
	if(McSaveIconSys==NULL){
		if((result=PrepareOSDResources(NULL, NULL, GameSettings->OSDTitleLine1, GameSettings->OSDTitleLine2, InstFileList))>=0){
			result=InstallOSDFiles(path, InstFileList);
		}
	}
	else{
		if(PrepareOSDResources(SaveDataPath, McSaveIconSys, GameSettings->OSDTitleLine1, GameSettings->OSDTitleLine2, InstFileList)==ICON_SOURCE_SAVE){
			result=((result=InstallOSDFiles(path, InstFileList))>=0)?ICON_SOURCE_SAVE:-1;
		}
		else result=ICON_SOURCE_DEFAULT;	//Leave the caller to decide on what to do next.
	}

	FreeOSDResources(InstFileList);

	return result;
}

int UpdateGameInstallationOSDResources(const char *partition, const wchar_t *OSDTitleLine1, const wchar_t *OSDTitleLine2){
	char path[38];
	OSDResFileEnt_t files[NUM_OSD_FILES_ENTS];
	void *FileBuffers[NUM_OSD_FILES_ENTS];
	struct OSDResourceFileEntry OSDFileEnts[NUM_OSD_FILES_ENTS];
	int result, i;
	struct IconSysData IconSys;
	unsigned char *HDDIconSys;

	sprintf(path, "hdd0:%s", partition);
	if((result=GetOSDResourceFileStats(path, files))==0){
		memset(FileBuffers, 0, sizeof(FileBuffers));
		memset(OSDFileEnts, 0, sizeof(OSDFileEnts));

		for(i=0,result=0; i<NUM_OSD_FILES_ENTS; i++){
			if(files[i].size>0){
				if((FileBuffers[i]=memalign(64, files[i].size))!=NULL){
					if((result=ReadOSDResourceFile(path, i, files, FileBuffers[i]))!=0){
						DEBUG_PRINTF("UpdateGameInstallationOSDResources: Failed to read OSD resource file %i: %d\n", i, result);
						break;
					}
				}
				else{
					result=-ENOMEM;
					DEBUG_PRINTF("UpdateGameInstallationOSDResources: Failed to allocate %d bytes of memory for OSD resource file %i: %d\n", files[i].size, i, result);
					break;
				}
			}
		}

		if(result==0 && FileBuffers[OSD_ICON_SYS_INDEX]!=NULL){
			if((result=LoadIconSysFile(FileBuffers[OSD_ICON_SYS_INDEX], files[OSD_ICON_SYS_INDEX].size, &IconSys))==0){
				//Update icon sys file.
				wcsncpy(IconSys.title0, OSDTitleLine1, OSD_TITLE_MAX_LEN);
				IconSys.title0[OSD_TITLE_MAX_LEN]='\0';
				wcsncpy(IconSys.title1, OSDTitleLine2, OSD_TITLE_MAX_LEN);
				IconSys.title1[OSD_TITLE_MAX_LEN]='\0';

				free(FileBuffers[OSD_ICON_SYS_INDEX]);

				/* Convert the icon file back into the HDD OSD format. */
				HDDIconSys=malloc(640);	/* Allocate sufficient memory to accommodate the longest possible title and the standard icon.sys file fields as in the template. */
				FileBuffers[OSD_ICON_SYS_INDEX]=memalign(64, files[OSD_ICON_SYS_INDEX].size=GenerateHDDIconSysFile(&IconSys, HDDIconSys)-1);
				memcpy(FileBuffers[OSD_ICON_SYS_INDEX], HDDIconSys, files[OSD_ICON_SYS_INDEX].size);
				free(HDDIconSys);

				PrepareOSDDefaultResources(OSDFileEnts);

				for(i=0; i<NUM_OSD_FILES_ENTS; i++){
					if(FileBuffers[i]!=NULL){
						LoadOSDResource(i, OSDFileEnts, FileBuffers[i], files[i].size, 1);
					}
				}

				//Write updated file back (Rebuild the partition attribute area).
				if((result=InstallOSDFiles(path, OSDFileEnts))<0){
					DEBUG_PRINTF("UpdateGameInstallationOSDResources: Failed to write OSD resource files: %d\n", result);
				}
			}
			else{	//Can't parse the icon.sys file.
				DEBUG_PRINTF("UpdateGameInstallationOSDResources: Failed to parse icon.sys file: %d\n", result);

				for(i=0; i<NUM_OSD_FILES_ENTS; i++){
					if(FileBuffers[i]!=NULL) free(FileBuffers[i]);
				}
			}
		}
		else{
			result=-1;	//partition attribute area is corrupted.
			for(i=0; i<NUM_OSD_FILES_ENTS; i++){
				if(FileBuffers[i]!=NULL) free(FileBuffers[i]);
			}
		}
	}
	else{
		DEBUG_PRINTF("UpdateGameInstallationOSDResources: Can't getstat the OSD resource files: %d\n", result);
	}

	return result;
}

struct IconInfoListBuilderStruct{
	struct IconInfoListBuilderStruct *next;
	struct IconInfo IconInfo;
};

int GetIconListFromDevice(const char *device, struct IconInfo **IconList){
	iox_dirent_t dirent;
	int FolderFD, fd, result, len;
	struct IconInfoListBuilderStruct *first, *end, *CurrentEntry;
	unsigned int NumIcons, i;
	mcIcon icon;
	char *FullPath;
	struct IconInfo *FullList;
	char deviceNamePath[9];

	result=0;
	NumIcons=0;
	first=end=NULL;
	*IconList=FullList=NULL;
	sprintf(deviceNamePath, "%s/", device);
	DEBUG_PRINTF("Scanning %s for icons.\n", deviceNamePath);
	if((FolderFD=fileXioDopen(deviceNamePath))>=0){
		while(fileXioDread(FolderFD, &dirent)>0){
			if(FIO_S_ISDIR(dirent.stat.mode) && strlen(dirent.name)<=sizeof(CurrentEntry->IconInfo.foldername)-1 && strcmp(dirent.name, ".") && strcmp(dirent.name, "..")){
				DEBUG_PRINTF("\t%s\t0x%04x\n", dirent.name, dirent.stat.mode);

				if((FullPath=malloc(strlen(deviceNamePath)+11+strlen(dirent.name)))!=NULL){
					sprintf(FullPath, "%s/%s/icon.sys", device, dirent.name);
					if((fd=fileXioOpen(FullPath, O_RDONLY))>=0){
						if(fileXioRead(fd, &icon, sizeof(icon))==sizeof(icon) && memcmp(icon.head, "PS2D", 4)==0){
							if((CurrentEntry=malloc(sizeof(struct IconInfoListBuilderStruct)))!=NULL){
								CurrentEntry->next=NULL;
								strncpy(CurrentEntry->IconInfo.foldername, dirent.name, sizeof(CurrentEntry->IconInfo.foldername)-1);
								CurrentEntry->IconInfo.foldername[sizeof(CurrentEntry->IconInfo.foldername)-1]='\0';
								len = SJISToUnicode((const char*)icon.title, sizeof(CurrentEntry->IconInfo.title)-1, CurrentEntry->IconInfo.title, sizeof(CurrentEntry->IconInfo.title)/sizeof(wchar_t)-1);
								CurrentEntry->IconInfo.title[len]='\0';

								if(first==NULL){
									first=CurrentEntry;
								}
								if(end!=NULL){
									end->next=CurrentEntry;
								}

								end=CurrentEntry;
								NumIcons++;
							}
							else{
								result=-ENOMEM;
								break;
							}
						}
						else{
							DEBUG_PRINTF("Invalid icon found.\n");
						}

						fileXioClose(fd);
					}

					free(FullPath);
				}
				else{
					result=-ENOMEM;
					break;
				}
			}
		}

		fileXioDclose(FolderFD);
	}
	else{
		DEBUG_PRINTF("Unable to open device.\n");
	}

	if(result>=0){
		if((FullList=malloc(NumIcons*sizeof(struct IconInfo)))!=NULL){
			CurrentEntry=first;
			for(i=0; CurrentEntry!=NULL; i++){
				memcpy(&FullList[i], &CurrentEntry->IconInfo, sizeof(FullList[i]));

				first=CurrentEntry->next;
				free(CurrentEntry);
				CurrentEntry=first;
			}

			*IconList=FullList;
		}
		else{
			result=-ENOMEM;
			NumIcons=0;
			CurrentEntry=first;
			while(CurrentEntry!=NULL){
				first=CurrentEntry->next;
				free(CurrentEntry);
				CurrentEntry=first;
			}
		}
	}

	return(result<0?result:NumIcons);
}

