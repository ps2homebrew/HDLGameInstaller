#include <kernel.h>
#include <libmc.h>
#include <libpad.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <osd_config.h>
#include <timer.h>
#include <limits.h>
#include <fileXio_rpc.h>
#include <netman.h>
#include <wchar.h>

#include <gsKit.h>

#include "main.h"
#ifdef ENABLE_NETWORK_SUPPORT
#include <ps2ip.h>
#include <netman.h>
#endif

#include "pad.h"
#include "graphics.h"
#include "font.h"
#include "UI.h"
#include "menu.h"
#include "OSD.h"
#include "IconLoader.h"
#include "IconRender.h"
#include "HDLGameList.h"
#include "system.h"
#include "settings.h"

extern GSGLOBAL *gsGlobal;
extern GSTEXTURE BackgroundTexture;
extern GSTEXTURE PadLayoutTexture;
extern GSTEXTURE DeviceIconTexture;
extern unsigned short int SelectButton, CancelButton;

extern struct RuntimeData RuntimeData;

static char FreeDiskSpaceDisplay[16];
static char IPAddressDisplay[16];	//XXX.XXX.XXX.XXX
static u8 mac_address[6];

static void DrawButton(const char *label, float x, float y, u64 TextColour, int IsSelected);
static int GetUserGameSettings(struct GameSettings *GameSettings);
static int InitIconPreview(const char *device, const char *icon, struct PS2IconModel *IconModel, struct IconModelAnimRuntimeData *IconRuntimeData, GSTEXTURE *texture);
static void DeinitIconPreview(struct PS2IconModel *IconModel, struct IconModelAnimRuntimeData *IconRuntimeData);
static int GetUserIconFileSelectionFromDevice(const char *device, int unit, char **IconPath);
static int GetUserIconFileSelection(char **IconPath);
static int GetUserIconSourceChoice(char **IconPath);
static void RedrawMainMenu(struct HDLGameEntry *HDLGameList, unsigned int NumHDLGames, unsigned int SelectedGameIndex, unsigned int GameListViewPortStart);
static void DeleteGame(struct HDLGameEntry *HDLGameList, unsigned int GameIndex);
static int UpdateGame(struct HDLGameEntry *HDLGameList, unsigned int GameIndex);
static void EnterRemoteClientMenu(struct RuntimeData *RuntimeData);
static int StartInstallGame(sceCdRMode *ReadMode);
static void UpdateNetworkStatus(void);
static void UpdateHardwareAddress(void);
static void ShowNetworkStatus(void);
static int ShowOptions(void);
static void DrawMenuExitAnimation(void);

/*
	In this whole file, some variables and values used would be:

	SelectedMenu ->	0 = The game list.
			1 = The control panel.

	SelectedMenuOption -> The element in the menu that is currently selected.
*/

enum ConfigMenuFieldTypes{
	CONFIG_MENU_FIELD_OPTION=0,	/* Normal "enabled/disabled" option. */
	CONFIG_MENU_FIELD_BLANK,	/* An open-ended option. */
	CONFIG_MENU_FIELD_RADIO,	/* A radio button option. */
};

struct ConfigMenuRadioOption{
	const char *label;
};

struct ConfigMenuFieldData{
	const char *label;
	unsigned int FieldType;
	unsigned short int x, y, ValueXOffset, ValueYOffset;
	void *value;
	int SelectedValue;
	unsigned int FieldLength;
};

struct GameListDisplayData{
	unsigned int SelectedGameIndex;
	unsigned int GameListViewPortStart;
};

struct MenuPageConfigData{
	unsigned int NumFields;
	struct ConfigMenuFieldData *fields;
};

struct MenuConfigData{
	unsigned short int NumPages;
	char FormIndex;	//Form index = the position of the form in a series of forms, where 0 = the first form.
	char NumForms;	//Number of forms in this set of forms.
	const struct MenuPageConfigData *pages;
};

static int ShowMenu(struct MenuConfigData *MenuConfigData, int (*MenuValidatorCallbackFunction)(struct MenuConfigData *MenuConfigData));
static int UserGameSettingsMenuValidator(struct MenuConfigData *MenuConfigData);
static unsigned int GetGameListData(struct HDLGameEntry **HDLGameList, struct GameListDisplayData *GameListDisplayData);
static unsigned int ReloadGameList(struct HDLGameEntry **HDLGameList, struct GameListDisplayData *GameListDisplayData);

void RedrawLoadingScreen(unsigned int frame)
{
	short int xRel, x, y;
	int NumDots;

	SyncFlipFB();

	NumDots=frame%240/60;

	DrawBackground(gsGlobal, &BackgroundTexture);
	FontPrintf(gsGlobal, 10, 10, 0, 2.5f, GS_WHITE_FONT, "HDLGameInstaller v"HDLGAME_INSTALLER_VERSION);

	x = 420;
	y = 380;
	FontPrintfWithFeedback(gsGlobal, x, y, 0, 1.8f, GS_WHITE_FONT, "Loading ", &xRel, NULL);
	x += xRel;
	switch(NumDots)
	{
		case 1:
			FontPrintf(gsGlobal, x, y, 0, 1.8f, GS_WHITE_FONT, ".");
			break;
		case 2:
			FontPrintf(gsGlobal, x, y, 0, 1.8f, GS_WHITE_FONT, ". .");
			break;
		case 3:
			FontPrintf(gsGlobal, x, y, 0, 1.8f, GS_WHITE_FONT, ". . .");
			break;
	}

	if(frame < 60)
	{	//Fade in
		gsKit_prim_quad(gsGlobal, 0.0f, 0.0f,
				gsGlobal->Width, 0.0f,
				0.0f, gsGlobal->Height,
				gsGlobal->Width, gsGlobal->Height,
				0, GS_SETREG_RGBAQ(0, 0, 0, 0x00+(frame*2), 0));
	}
}

/* Warning: Do not specify a label longer than MAX_BTN_LAB_LEN. */
static void DrawButton(const char *label, float x, float y, u64 TextColour, int IsSelected){
	gsKit_prim_quad(gsGlobal, x, y, x+240, y, x, y+20, x+240, y+20, 2, IsSelected?GS_LGREY:GS_GREY);
	/* Draw the label. Centre it. */
	FontPrintf(gsGlobal, (MAX_BTN_LAB_LEN-mbslen(label))*BTN_FNT_CHAR_WIDTH+x, y, 1, 1.0f, TextColour, label);
}

static int ShowMenu(struct MenuConfigData *MenuConfigData, int (*MenuValidatorCallbackFunction)(struct MenuConfigData *MenuConfigData)){
	int result, CurrentPage, SelectedOption;
	unsigned char done;
	unsigned int PadStatus, i;
	u64 FontColour;
	const struct MenuPageConfigData *CurrentPageConfig;
	const char *ConfirmButtonText, *CancelButtonText;

	if(MenuConfigData->FormIndex==0){	//First form
		ConfirmButtonText=GetUILabel(SYS_UI_LBL_NEXT);
		CancelButtonText=GetUILabel(SYS_UI_LBL_CANCEL);
	}
	else if(MenuConfigData->FormIndex==MenuConfigData->NumForms-1){	//Last form
		ConfirmButtonText=GetUILabel(SYS_UI_LBL_OK);
		CancelButtonText=GetUILabel(SYS_UI_LBL_BACK);
	}
	else{	//Somewhere in the middle.
		ConfirmButtonText=GetUILabel(SYS_UI_LBL_NEXT);
		CancelButtonText=GetUILabel(SYS_UI_LBL_BACK);
	}

	done=0;
	result=0;
	SelectedOption=0;
	CurrentPage=0;
	CurrentPageConfig=&MenuConfigData->pages[0];
	do{
		DrawBackground(gsGlobal, &BackgroundTexture);

		for(i=0; i<CurrentPageConfig->NumFields; i++){
			FontPrintf(gsGlobal, CurrentPageConfig->fields[i].x, CurrentPageConfig->fields[i].y, 1, 1.0f, GS_WHITE_FONT, CurrentPageConfig->fields[i].label);
			switch(CurrentPageConfig->fields[i].FieldType){
				case CONFIG_MENU_FIELD_OPTION:
					FontColour=(i==SelectedOption)?GS_YELLOW_FONT:GS_BLUE_FONT;
					FontPrintf(gsGlobal, CurrentPageConfig->fields[i].x+CurrentPageConfig->fields[i].ValueXOffset, CurrentPageConfig->fields[i].y+CurrentPageConfig->fields[i].ValueYOffset, 1, 1.0f, FontColour, CurrentPageConfig->fields[i].SelectedValue==1?GetUILabel(SYS_UI_LBL_ENABLED):GetUILabel(SYS_UI_LBL_DISABLED));
					break;
				case CONFIG_MENU_FIELD_BLANK:
					FontColour=(i==SelectedOption)?GS_YELLOW_FONT:GS_BLUE_FONT;
					wFontPrintTitle(gsGlobal, CurrentPageConfig->fields[i].x+CurrentPageConfig->fields[i].ValueXOffset, CurrentPageConfig->fields[i].y+CurrentPageConfig->fields[i].ValueYOffset+16, 1, 1.0f, FontColour, CurrentPageConfig->fields[i].value, MENU_BLANK_MAX_WIDTH);
					FontColour=(i==SelectedOption)?GS_YELLOW:GS_BLUE;
					gsKit_prim_line(gsGlobal, CurrentPageConfig->fields[i].x+CurrentPageConfig->fields[i].ValueXOffset, CurrentPageConfig->fields[i].y+CurrentPageConfig->fields[i].ValueYOffset+32, 620, CurrentPageConfig->fields[i].y+CurrentPageConfig->fields[i].ValueYOffset+32, 1, FontColour);
					break;
				case CONFIG_MENU_FIELD_RADIO:
					FontColour=(i==SelectedOption)?GS_YELLOW_FONT:GS_BLUE_FONT;
					FontPrintf(gsGlobal, CurrentPageConfig->fields[i].x+CurrentPageConfig->fields[i].ValueXOffset, CurrentPageConfig->fields[i].y+CurrentPageConfig->fields[i].ValueYOffset, 1, 1.0f, FontColour, ((struct ConfigMenuRadioOption*)CurrentPageConfig->fields[i].value)[CurrentPageConfig->fields[i].SelectedValue].label);
					break;
			}
		}

		DrawButton(ConfirmButtonText, 20, 360, GS_WHITE_FONT, SelectedOption==-1?1:0);
		DrawButton(CancelButtonText, 320, 360, GS_WHITE_FONT, SelectedOption==-2?1:0);

		/* Draw the legend. */
		DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_UD_DPAD, 20, 390, 4);
		FontPrintf(gsGlobal, 60, 390, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_SELECT_FIELD));
		DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_LR_DPAD, 20, 420, 4);
		FontPrintf(gsGlobal, 60, 420, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_TOGGLE_OPTION));
		DrawButtonLegend(gsGlobal, &PadLayoutTexture, SelectButton == PAD_CIRCLE ? BUTTON_TYPE_CIRCLE : BUTTON_TYPE_CROSS, 300, 400, 4);
		FontPrintf(gsGlobal, 324, 400, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_OK));
		DrawButtonLegend(gsGlobal, &PadLayoutTexture, CancelButton == PAD_CIRCLE ? BUTTON_TYPE_CIRCLE : BUTTON_TYPE_CROSS, 380, 400, 4);
		FontPrintf(gsGlobal, 404, 400, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_CANCEL));

		if(MenuConfigData->NumPages>1){
			if(CurrentPage>0){
				DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_L1, 10, 320, 4);
			}
			if(CurrentPage<MenuConfigData->NumPages-1){
				DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_R1, 620, 320, 4);
			}
		}

		PadStatus=ReadCombinedPadStatus();

		if(PadStatus&PAD_START){
			SelectedOption=-1;	/* Highlight the confirm button. */
		}
		if(PadStatus&CancelButton){
			SelectedOption=-2;
		}

		/* If the user did not select the confirm or cancel buttons */
		if(SelectedOption!=-1 && SelectedOption!=-2){
			if(PadStatus&SelectButton && CurrentPageConfig->fields[SelectedOption].FieldType==CONFIG_MENU_FIELD_BLANK){
				DisplaySoftKeyboard(CurrentPageConfig->fields[SelectedOption].value, CurrentPageConfig->fields[SelectedOption].FieldLength, 0);
			}

			if((PadStatus&PAD_L1) || (PadStatus&PAD_R1)){
				if(PadStatus&PAD_L1){
					if(CurrentPage>0) CurrentPage--;
				}
				else if(PadStatus&PAD_R1){
					if(CurrentPage<MenuConfigData->NumPages-1) CurrentPage++;
				}

				CurrentPageConfig=&MenuConfigData->pages[CurrentPage];
				SelectedOption=0;
			}
			else if(PadStatus&PAD_UP){
				if(SelectedOption>0) SelectedOption--;
			}
			else if(PadStatus&PAD_DOWN){
				if(SelectedOption<CurrentPageConfig->NumFields-1){
					SelectedOption++;
				}
				else{
					SelectedOption=-1;
				}
			}

			switch(CurrentPageConfig->fields[SelectedOption].FieldType){
				case CONFIG_MENU_FIELD_OPTION:
					if(PadStatus&PAD_LEFT && CurrentPageConfig->fields[SelectedOption].SelectedValue==1){
						CurrentPageConfig->fields[SelectedOption].SelectedValue=0;
					}
					if(PadStatus&PAD_RIGHT && CurrentPageConfig->fields[SelectedOption].SelectedValue==0){
						CurrentPageConfig->fields[SelectedOption].SelectedValue=1;
					}
					break;
				case CONFIG_MENU_FIELD_BLANK:
					break;
				case CONFIG_MENU_FIELD_RADIO:
					if(PadStatus&PAD_LEFT && CurrentPageConfig->fields[SelectedOption].SelectedValue>0){
						CurrentPageConfig->fields[SelectedOption].SelectedValue--;
					}
					if(PadStatus&PAD_RIGHT && CurrentPageConfig->fields[SelectedOption].SelectedValue<CurrentPageConfig->fields[SelectedOption].FieldLength-1){
						CurrentPageConfig->fields[SelectedOption].SelectedValue++;
					}
					break;
			}
		}
		/* Otherwise, the user has selected either the "OK" or "Cancel" buttons */
		else{
			if(PadStatus&SelectButton || PadStatus&CancelButton){
				if(SelectedOption==-1 && (PadStatus&SelectButton)){
					result=0;

					if((MenuValidatorCallbackFunction!=NULL && MenuValidatorCallbackFunction(MenuConfigData)==0) || (MenuValidatorCallbackFunction==NULL)){
						if(DisplayPromptMessage(SYS_UI_MSG_PROCEED, SYS_UI_LBL_CANCEL, SYS_UI_LBL_OK)==2){
							done=1;
						}
					}
				}
				else if(SelectedOption==-2 && (PadStatus&CancelButton || PadStatus&SelectButton)){
					if(DisplayPromptMessage(SYS_UI_MSG_CANCEL_INPUT, SYS_UI_LBL_CANCEL, SYS_UI_LBL_OK)==2){
						result=1;
						done=1;
					}
				}
			}
			else if(PadStatus&PAD_UP){
				SelectedOption=CurrentPageConfig->NumFields-1;
			}
			else if(PadStatus&PAD_LEFT && SelectedOption==-2){
				SelectedOption=-1;
			}
			else if(PadStatus&PAD_RIGHT && SelectedOption==-1){
				SelectedOption=-2;
			}
		}

		SyncFlipFB();
	}while(!done);

	return result;
}

#define USER_GAME_SETTINGS_MENU_NUM_OPTIONS	12

static int UserGameSettingsMenuValidator(struct MenuConfigData *MenuConfigData){
	int result;
	const struct MenuPageConfigData *CurrentPageConfig;

	CurrentPageConfig=&MenuConfigData->pages[0];
	result=0;

	if(wcslen(CurrentPageConfig->fields[0].value)<1){
		DisplayErrorMessage(SYS_UI_MSG_FULL_GAME_TITLE);
		result=1;
	}
	if(wcslen(CurrentPageConfig->fields[1].value)<1){
		DisplayErrorMessage(SYS_UI_MSG_OSD_TITLE_1);
		result=1;
	}

	return result;
}

static int GetUserGameSettings(struct GameSettings *GameSettings){
	int result;
	unsigned int i;
	static struct ConfigMenuFieldData UserGameSettingsMenuFields[USER_GAME_SETTINGS_MENU_NUM_OPTIONS]={
		{
			NULL,
			CONFIG_MENU_FIELD_BLANK,
			10, 48, 0, 0, NULL, 0, GAME_TITLE_MAX_LEN
		},
		{
			NULL,
			CONFIG_MENU_FIELD_BLANK,
			10, 96, 0, 0, NULL, 0, OSD_TITLE_MAX_LEN
		},
		{
			NULL,
			CONFIG_MENU_FIELD_BLANK,
			10, 144, 0, 0, NULL, 0, OSD_TITLE_MAX_LEN
		},
		{	/* OPL compatibility mode 1 */
			NULL,
			CONFIG_MENU_FIELD_OPTION,
			10, 192, 440, 0, NULL, 0, 4
		},
		{	/* OPL compatibility mode 2 */
			NULL,
			CONFIG_MENU_FIELD_OPTION,
			10, 208, 440, 0, NULL, 0, 4
		},
		{	/* OPL compatibility mode 3 */
			NULL,
			CONFIG_MENU_FIELD_OPTION,
			10, 224, 440, 0, NULL, 0, 4
		},
		{	/* OPL compatibility mode 4 */
			NULL,
			CONFIG_MENU_FIELD_OPTION,
			10, 240, 440, 0, NULL, 0, 4
		},
		{	/* OPL compatibility mode 5 */
			NULL,
			CONFIG_MENU_FIELD_OPTION,
			10, 256, 440, 0, NULL, 0, 4
		},
		{	/* OPL compatibility mode 6 */
			NULL,
			CONFIG_MENU_FIELD_OPTION,
			10, 272, 440, 0, NULL, 0, 4
		},
		{	/* OPL compatibility mode 7 */
			NULL,
			CONFIG_MENU_FIELD_OPTION,
			10, 288, 440, 0, NULL, 0, 4
		},
		{	/* OPL compatibility mode 8 */
			NULL,
			CONFIG_MENU_FIELD_OPTION,
			10, 304, 440, 0, NULL, 0, 4
		},
		{
			NULL,
			CONFIG_MENU_FIELD_OPTION,
			10, 320, 440, 0, NULL, 0, 4
		}
	};
	static const unsigned int MainMenuStringIDs[USER_GAME_SETTINGS_MENU_NUM_OPTIONS]={
		SYS_UI_LBL_INST_FULL_TITLE,
		SYS_UI_LBL_INST_OSD_TITLE_1,
		SYS_UI_LBL_INST_OSD_TITLE_2,
		SYS_UI_LBL_INST_OPTION_1,
		SYS_UI_LBL_INST_OPTION_2,
		SYS_UI_LBL_INST_OPTION_3,
		SYS_UI_LBL_INST_OPTION_4,
		SYS_UI_LBL_INST_OPTION_5,
		SYS_UI_LBL_INST_OPTION_6,
		SYS_UI_LBL_INST_OPTION_7,
		SYS_UI_LBL_INST_OPTION_8,
		SYS_UI_LBL_INST_TRANSFER_OPTION,
	};
	static const struct MenuPageConfigData MenuPageConfigData={
		USER_GAME_SETTINGS_MENU_NUM_OPTIONS,
		UserGameSettingsMenuFields
	};
	static struct MenuConfigData MenuConfigData={
		1, 0, 2,
		&MenuPageConfigData
	};

	DEBUG_PRINTF("-= Gathering user game settings =-\n");

	result=0;
	for(i=0; i<USER_GAME_SETTINGS_MENU_NUM_OPTIONS; i++){
		switch(UserGameSettingsMenuFields[i].FieldType){
			case CONFIG_MENU_FIELD_OPTION:
			case CONFIG_MENU_FIELD_RADIO:
				UserGameSettingsMenuFields[i].value=NULL;
				UserGameSettingsMenuFields[i].SelectedValue=0;
				break;
			case CONFIG_MENU_FIELD_BLANK:
				UserGameSettingsMenuFields[i].value=malloc((UserGameSettingsMenuFields[i].FieldLength+1)*sizeof(wchar_t));
				memset(UserGameSettingsMenuFields[i].value, 0, (UserGameSettingsMenuFields[i].FieldLength+1)*sizeof(wchar_t));
				break;
		}

		UserGameSettingsMenuFields[i].label=GetUILabel(MainMenuStringIDs[i]);
	}

	/* Set default options. */
	wcsncpy(UserGameSettingsMenuFields[0].value, GameSettings->FullTitle, UserGameSettingsMenuFields[0].FieldLength);
	wcsncpy(UserGameSettingsMenuFields[1].value, GameSettings->OSDTitleLine1, UserGameSettingsMenuFields[1].FieldLength);
	wcsncpy(UserGameSettingsMenuFields[2].value, GameSettings->OSDTitleLine2, UserGameSettingsMenuFields[2].FieldLength);
	for(i=0; i<=8; i++) UserGameSettingsMenuFields[i+3].SelectedValue=(GameSettings->CompatibilityModeFlags)>>i&1;
	UserGameSettingsMenuFields[11].SelectedValue=GameSettings->UseMDMA0;

	result=ShowMenu(&MenuConfigData, &UserGameSettingsMenuValidator);

	/* Process the user's input. */
	if(result==0){
		wcsncpy(GameSettings->FullTitle, UserGameSettingsMenuFields[0].value, GAME_TITLE_MAX_LEN);
		GameSettings->FullTitle[GAME_TITLE_MAX_LEN]='\0';
		wcsncpy(GameSettings->OSDTitleLine1, UserGameSettingsMenuFields[1].value, OSD_TITLE_MAX_LEN);
		GameSettings->OSDTitleLine1[OSD_TITLE_MAX_LEN]='\0';
		wcsncpy(GameSettings->OSDTitleLine2, UserGameSettingsMenuFields[2].value, OSD_TITLE_MAX_LEN);
		GameSettings->OSDTitleLine2[OSD_TITLE_MAX_LEN]='\0';
		GameSettings->CompatibilityModeFlags=0;
		for(i=0; i<=8; i++) GameSettings->CompatibilityModeFlags|=UserGameSettingsMenuFields[i+3].SelectedValue<<i;

		GameSettings->UseMDMA0=UserGameSettingsMenuFields[11].SelectedValue;
	}

	/* Free allocated memory. */
	for(i=0; i<USER_GAME_SETTINGS_MENU_NUM_OPTIONS; i++){
		if(UserGameSettingsMenuFields[i].value!=NULL) free(UserGameSettingsMenuFields[i].value);
	}

	return result;
}

#define MAX_ENTRIES_IN_LIST	12

static int InitIconPreview(const char *device, const char *icon, struct PS2IconModel *IconModel, struct IconModelAnimRuntimeData *IconRuntimeData, GSTEXTURE *texture){
	char *fullpath, filename[65];
	mcIcon McSaveIconSys;
	int result;

	if((fullpath=malloc(strlen(device)+strlen(icon)+35))!=NULL){
		sprintf(fullpath, "%s/%s", device, icon);

		if((result=LoadMcSaveSysFromPath(fullpath, &McSaveIconSys))==0){
			strncpy(filename, McSaveIconSys.view, sizeof(filename)-1);
			filename[sizeof(filename)-1]='\0';
			sprintf(fullpath, "%s/%s/%s", device, icon, filename);

			if((result=LoadPS2IconModel(fullpath, IconModel))==0){
				UploadIcon(IconModel, texture);
				InitIconModelRuntimeData(IconModel, IconRuntimeData);
				result=0;
			}
		}

		free(fullpath);
	}
	else result=-ENOMEM;

	return result;
}

static void DeinitIconPreview(struct PS2IconModel *IconModel, struct IconModelAnimRuntimeData *IconRuntimeData){
	FreeIconModelRuntimeData(IconModel, IconRuntimeData);
	UnloadPS2IconModel(IconModel);
	memset(IconModel, 0, sizeof(struct PS2IconModel));
	memset(IconRuntimeData, 0, sizeof(struct IconModelAnimRuntimeData));
}

static int GetUserIconFileSelectionFromDevice(const char *device, int unit, char **IconPath){
	struct IconInfo *IconList;
	unsigned char done;
	unsigned int i, PadStatus, ListStartIndex, SelectedIndexInList, FrameNumber;
	int NumIcons, result;
	struct PS2IconModel IconModel;
	struct IconModelAnimRuntimeData IconRuntimeData;
	GSTEXTURE IconTexture;
	char SelectedDeviceName[8];

	sprintf(SelectedDeviceName, "%s%u:", device, unit);
	NumIcons=GetIconListFromDevice(SelectedDeviceName, &IconList);
	DEBUG_PRINTF("Loaded, num icons: %d\n", NumIcons);

	done=0;
	ListStartIndex=0;
	SelectedIndexInList=0;
	result=1;
	*IconPath=NULL;
	FrameNumber=0;

	ReinitializeUI();

	memset(&IconModel, 0, sizeof(IconModel));
	memset(&IconRuntimeData, 0, sizeof(IconRuntimeData));
	memset(&IconTexture, 0, sizeof(IconTexture));

	IconTexture.Vram=gsKit_vram_alloc(gsGlobal, gsKit_texture_size(128, 128, GS_PSM_CT16), GSKIT_ALLOC_USERBUFFER);

	if(NumIcons>0){
		InitIconPreview(SelectedDeviceName, IconList[ListStartIndex+SelectedIndexInList].foldername, &IconModel, &IconRuntimeData, &IconTexture);
	}
	while(!done){
		DrawBackground(gsGlobal, &BackgroundTexture);

		PadStatus=ReadCombinedPadStatus();

		FontPrintf(gsGlobal, 16, 16, 1, 1.2f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_SELECT_ICON));

		if(GetIsDeviceUnitReady(device, unit)!=1){
			//Unit was disconnected.
			done=1;
			result=1;
		}

		if(NumIcons>0){
			/* Draw the highlight. */
			gsKit_prim_quad(gsGlobal, 40, 64+SelectedIndexInList*16, 40, 64+SelectedIndexInList*16+16, 540, 64+SelectedIndexInList*16, 540, 64+SelectedIndexInList*16+16, 8, GS_LRED_TRANS);

			for(i=0; i<MAX_ENTRIES_IN_LIST; i++){
				if(ListStartIndex+i<NumIcons){
					wFontPrintf(gsGlobal, 40, 64+i*16, 1, 1.0f, GS_WHITE_FONT, IconList[ListStartIndex+i].title);
				}
				else break;
			}

			//Draw the icon preview.
			TransformIcon(FrameNumber, 400.0, 128.0f, 0, 4.0f, &IconModel, &IconRuntimeData);
			DrawIcon(&IconModel, &IconRuntimeData, &IconTexture);

			// Draw the legend.
			DrawButtonLegend(gsGlobal, &PadLayoutTexture, SelectButton == PAD_CIRCLE ? BUTTON_TYPE_CIRCLE : BUTTON_TYPE_CROSS, 50, 404, 2);
			FontPrintf(gsGlobal, 75, 406, 2, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_OK));
			DrawButtonLegend(gsGlobal, &PadLayoutTexture, CancelButton == PAD_CIRCLE ? BUTTON_TYPE_CIRCLE : BUTTON_TYPE_CROSS, 200, 404, 2);
			FontPrintf(gsGlobal, 240, 406, 2, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_CANCEL));

			if(PadStatus&(PAD_UP|PAD_DOWN)){
				if(PadStatus&PAD_UP){
					if(SelectedIndexInList>0) SelectedIndexInList--;
					else{
						if(ListStartIndex>0) ListStartIndex--;
					}
				}
				else if(PadStatus&PAD_DOWN){
					if(SelectedIndexInList<MAX_ENTRIES_IN_LIST-1 && SelectedIndexInList<NumIcons-1) SelectedIndexInList++;
					else{
						if(ListStartIndex+MAX_ENTRIES_IN_LIST<NumIcons) ListStartIndex++;
					}
				}

				DeinitIconPreview(&IconModel, &IconRuntimeData);
				InitIconPreview(SelectedDeviceName, IconList[ListStartIndex+SelectedIndexInList].foldername, &IconModel, &IconRuntimeData, &IconTexture);
			}
			if(PadStatus&SelectButton){
				if((*IconPath=malloc(strlen(SelectedDeviceName)+strlen(IconList[ListStartIndex+SelectedIndexInList].foldername)+2))!=NULL){
					sprintf(*IconPath, "%s/%s", SelectedDeviceName, IconList[ListStartIndex+SelectedIndexInList].foldername);
					result=0;
				}
				else result=-ENOMEM;
				done=1;
			}
		}
		else{
			FontPrintf(gsGlobal, 40, 64, 1, 1.0f, GS_WHITE_FONT, GetUIString(SYS_UI_MSG_NO_ICONS));
		}

		if(PadStatus&CancelButton){
			result=1;
			done=1;
		}

		SyncFlipFB();
		FrameNumber++;
	}

	if(IconList!=NULL) free(IconList);
	DeinitIconPreview(&IconModel, &IconRuntimeData);

	ReinitializeUI();

	return result;
}

#define NUM_SUPPORTED_DEVICES	3

struct SupportedDevice{
	const char *name;
	const char *label, *UnitLabel;
	unsigned char unit;
	unsigned char icon;
	unsigned char IsReady;
};

static int GetUserIconFileSelection(char **IconPath){
	unsigned char done, NumDevicesAvailable, DeviceID;
	unsigned int PadStatus;
	int result, i, devicesInRow, deviceRow, SelectedDevice;
	u64 FontColour;
	static struct SupportedDevice devices[NUM_SUPPORTED_DEVICES]={
		{
			"mc",
			NULL, NULL,
			0,
			DEVICE_TYPE_DISK,
			0,
		},
		{
			"mc",
			NULL, NULL,
			1,
			DEVICE_TYPE_DISK,
			0,
		},
		{
			"mass",
			NULL, NULL,
			0,
			DEVICE_TYPE_USB_DISK,
			0,
		}
	};
	static const unsigned int IconFileSelMenuDevStringIDs[NUM_SUPPORTED_DEVICES]={
		SYS_UI_LBL_DEV_MC,
		SYS_UI_LBL_DEV_MC,
		SYS_UI_LBL_DEV_MASS
	};
	static const unsigned int IconFileSelMenuDevUnitStringIDs[NUM_SUPPORTED_DEVICES]={
		SYS_UI_LBL_MC_SLOT_1,
		SYS_UI_LBL_MC_SLOT_2,
		SYS_UI_LBL_COUNT
	};

	//Allow the user to browse for icon sets on mc0:, mc1: and mass:.

	for(i=0; i<NUM_SUPPORTED_DEVICES; i++){
		devices[i].label=GetUILabel(IconFileSelMenuDevStringIDs[i]);
		devices[i].UnitLabel=IconFileSelMenuDevUnitStringIDs[i]!=SYS_UI_LBL_COUNT?GetUILabel(IconFileSelMenuDevUnitStringIDs[i]):NULL;
	}

	StartDevicePollingThread();

	done=0;
	SelectedDevice=0;
	result=0;
	do{
		DrawBackground(gsGlobal, &BackgroundTexture);

		for(i=0,NumDevicesAvailable=0; i<NUM_SUPPORTED_DEVICES; i++){
			if(GetIsDeviceUnitReady(devices[i].name, devices[i].unit)==1){
				devices[i].IsReady=1;
				NumDevicesAvailable++;
			}
			else devices[i].IsReady=0;
		}

		PadStatus=ReadCombinedPadStatus();

		FontPrintf(gsGlobal, 16, 16, 1, 1.2f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_SELECT_DEVICE));

		// Draw the legend.
		DrawButtonLegend(gsGlobal, &PadLayoutTexture, SelectButton == PAD_CIRCLE ? BUTTON_TYPE_CIRCLE : BUTTON_TYPE_CROSS, 50, 404, 2);
		FontPrintf(gsGlobal, 75, 406, 2, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_OK));
		DrawButtonLegend(gsGlobal, &PadLayoutTexture, CancelButton == PAD_CIRCLE ? BUTTON_TYPE_CIRCLE : BUTTON_TYPE_CROSS, 200, 404, 2);
		FontPrintf(gsGlobal, 240, 406, 2, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_CANCEL));

		if(NumDevicesAvailable>0){
			if(SelectedDevice>=NumDevicesAvailable){
				SelectedDevice=NumDevicesAvailable-1;
			}

			if(PadStatus&PAD_LEFT){
				if(SelectedDevice>0) SelectedDevice--;
			}
			else if(PadStatus&PAD_RIGHT){
				if(SelectedDevice<NumDevicesAvailable-1) SelectedDevice++;
			}
			if(PadStatus&PAD_UP){
				if(SelectedDevice-MAX_DEVICES_IN_ROW>=0) SelectedDevice-=MAX_DEVICES_IN_ROW;
			}
			else if(PadStatus&PAD_DOWN){
				if(SelectedDevice+MAX_DEVICES_IN_ROW<=NumDevicesAvailable-1) SelectedDevice+=MAX_DEVICES_IN_ROW;
			}

			//Display a list of available devices and allow the user to choose a device to browse for icons from.
			for(i=0,DeviceID=0,devicesInRow=0,deviceRow=0; i<NumDevicesAvailable; DeviceID++)
			{
				if(devices[DeviceID].IsReady)
				{
					FontColour=(i==SelectedDevice)?GS_YELLOW_FONT:GS_BLUE_FONT;
					DrawDeviceIcon(gsGlobal, &DeviceIconTexture, devices[DeviceID].icon, DEVICE_LIST_X+32+200*devicesInRow, DEVICE_LIST_Y+100*deviceRow, 1);
					FontPrintf(gsGlobal, DEVICE_LIST_X+200*devicesInRow, DEVICE_LIST_Y+48+100*deviceRow, 1, 1.0f, FontColour, devices[DeviceID].label);
					if(devices[DeviceID].UnitLabel!=NULL) FontPrintf(gsGlobal, DEVICE_LIST_X+200*devicesInRow, DEVICE_LIST_Y+64+100*deviceRow, 1, 1.0f, FontColour, devices[DeviceID].UnitLabel);
					i++;
					devicesInRow++;
					if(devicesInRow == MAX_DEVICES_IN_ROW)
					{
						devicesInRow = 0;
						deviceRow++;
					}
				}
			}

			if(PadStatus&SelectButton){
				for(i=0,DeviceID=0; DeviceID<NUM_SUPPORTED_DEVICES; DeviceID++){
					if(devices[DeviceID].IsReady){
						if(i==SelectedDevice){
							break;
						}

						i++;
					}
				}
				SelectedDevice=DeviceID;

				StopDevicePollingThread();	//Polling the MCs will cause a deadlock. Didn't look much into it though.
				if((result=GetUserIconFileSelectionFromDevice(devices[SelectedDevice].name, devices[SelectedDevice].unit, IconPath))==0){
					result=0;
					done=1;
				}
				StartDevicePollingThread();
			}
		}

		if(PadStatus&CancelButton){
			result=1;
			done=1;
		}

		SyncFlipFB();
	}while(!done);

	StopDevicePollingThread();

	return result;
}

#define USER_ICON_CHOICE_MENU_NUM_OPTIONS	1

static int GetUserIconSourceChoice(char **IconPath){
	int result, i;
	static const unsigned int IconSourceChoiceMenuStringIDs[]={
		SYS_UI_LBL_ICON_SEL_DEFAULT,
		SYS_UI_LBL_ICON_SEL_SAVE_DATA,
		SYS_UI_LBL_ICON_SEL_EXTERNAL
	};
	static struct ConfigMenuRadioOption IconSourceRadioOptions[3];
	static struct ConfigMenuFieldData UserIconSettingsMenuFields[USER_GAME_SETTINGS_MENU_NUM_OPTIONS]={
		{
			NULL,
			CONFIG_MENU_FIELD_RADIO,
			10, 200, 400, 0, IconSourceRadioOptions, 0, 3
		}
	};
	static const struct MenuPageConfigData MenuPageConfigData={
		USER_ICON_CHOICE_MENU_NUM_OPTIONS,
		UserIconSettingsMenuFields
	};
	static struct MenuConfigData MenuConfigData={
		1, 1, 2,
		&MenuPageConfigData
	};

	for(i=0; i<3; i++){
		IconSourceRadioOptions[i].label=GetUILabel(IconSourceChoiceMenuStringIDs[i]);
	}

	UserIconSettingsMenuFields[0].label=GetUILabel(SYS_UI_LBL_ICON_SOURCE);

RedisplayMenu:
	if((result=ShowMenu(&MenuConfigData, NULL))==0){
		switch(UserIconSettingsMenuFields[0].SelectedValue){
			case 0:
			case 1:
				*IconPath=NULL;
				break;
			case 2:
				if(GetUserIconFileSelection(IconPath)!=0){
					goto RedisplayMenu;
				}
				break;
		}

		result=UserIconSettingsMenuFields[0].SelectedValue;
	}
	else result=-1;

	return result;
}

void DrawInstallGameScreen(const wchar_t *GameTitle, const char *DiscID, unsigned char DiscType, float percentage, unsigned int rate, unsigned int SecondsRemaining){
	char CharBuffer[32];
	float ProgressBarFillEndX;
	unsigned int HoursRemaining;
	unsigned char MinutesRemaining;

	FontPrintf(gsGlobal, 5, 5, 1, 1.5f, GS_WHITE_FONT, "HDLGame Installer");

	FontPrintf(gsGlobal, 10, 64, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_NOW_INSTALLING));
	FontPrintf(gsGlobal, 10, 94, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_TITLE));
	wFontPrintf(gsGlobal, 190, 94, 1, 1.0f, GS_WHITE_FONT, GameTitle);
	FontPrintf(gsGlobal, 10, 114, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_DISC_ID));
	FontPrintf(gsGlobal, 190, 114, 1, 1.0f, GS_WHITE_FONT, DiscID);
	FontPrintf(gsGlobal, 10, 134, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_DISC_TYPE));
	FontPrintf(gsGlobal, 190, 134, 1, 1.0f, GS_WHITE_FONT, DiscType==0x14?"DVD":"CD-ROM");
	FontPrintf(gsGlobal, 10, 154, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_RATE));
	sprintf(CharBuffer, "%uKB/s", rate);
	FontPrintf(gsGlobal, 190, 154, 1, 1.0f, GS_WHITE_FONT, CharBuffer);
	FontPrintf(gsGlobal, 10, 174, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_TIME_REMAINING));
	HoursRemaining=SecondsRemaining/3600;
	MinutesRemaining=(SecondsRemaining-HoursRemaining*3600)/60;
	if(SecondsRemaining<UINT_MAX){
		sprintf(CharBuffer, "%02u:%02u:%02u", HoursRemaining, MinutesRemaining, SecondsRemaining-HoursRemaining*3600-MinutesRemaining*60);
	}
	else{
		strcpy(CharBuffer, "--:--:--");
	}
	FontPrintf(gsGlobal, 190, 184, 1, 1.0f, GS_WHITE_FONT, CharBuffer);

	/* Draw the progress bar. */
	DrawProgressBar(gsGlobal, percentage, PROGRESS_BAR_START_X, PROGRESS_BAR_START_Y, 4, PROGRESS_BAR_LENGTH, GS_BLUE);

	DrawButtonLegend(gsGlobal, &PadLayoutTexture, CancelButton == PAD_CIRCLE ? BUTTON_TYPE_CIRCLE : BUTTON_TYPE_CROSS, 240, 360, 4);
	FontPrintf(gsGlobal, 280, 362, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_CANCEL));
}

static void RedrawMainMenu(struct HDLGameEntry *HDLGameList, unsigned int NumHDLGames, unsigned int SelectedGameIndex, unsigned int GameListViewPortStart)
{
	int len, lineMax;
	unsigned int i, SelectedGameListItem, NumGamesToDisplay, ScrollOffset;
	wchar_t title[GAME_TITLE_MAX_LEN+1];
	static unsigned int PausedFrames = 0, frames = 0, LastSelectedItem=0;

	SelectedGameListItem=SelectedGameIndex-GameListViewPortStart;

	FontPrintf(gsGlobal, 5, 5, 1, 1.8f, GS_WHITE_FONT, "HDLGame Installer");
	FontPrintf(gsGlobal, 380, 16, 1, 1.0f, GS_WHITE_FONT, FreeDiskSpaceDisplay);
#ifdef ENABLE_NETWORK_SUPPORT
	FontPrintf(gsGlobal, 480, 16, 1, 1.0f, GS_WHITE_FONT, IPAddressDisplay);
#else
	FontPrintf(gsGlobal, 480, 16, 1, 1.0f, GS_WHITE_FONT, "000.000.000.000");
#endif

	/**********************************************************************************/
	/* Draw the outline of the table. Each row is 16 pixels tall. */

	/* Draw the top of the table... */
	gsKit_prim_line(gsGlobal, 5.0f, 54.0f, 5.0f+GAME_LIST_WIDTH+5, 54.0f, 4, GS_WHITE);
	gsKit_prim_quad(gsGlobal, 5.0f, 50.0f, 5.0f, 53.0f, 5.0f+GAME_LIST_WIDTH+5, 50.0f, 5.0f+GAME_LIST_WIDTH+5, 53.0f, 4, GS_GREY);

	/* Draw left side */
	gsKit_prim_line(gsGlobal, 10.0f, 54.0f, 10.0f, 311.0f, 4, GS_WHITE);
	gsKit_prim_quad(gsGlobal, 5.0f, 50.0f, 10.0f, 50.0f, 5.0f, 312.0f, 10.0f, 312.0f, 4, GS_GREY);

	/* Draw right side */
	gsKit_prim_line(gsGlobal, 5.0f+GAME_LIST_WIDTH-1, 54.0f, 5.0f+GAME_LIST_WIDTH-1, 311.0f, 4, GS_WHITE);
	gsKit_prim_quad(gsGlobal, 5.0f+GAME_LIST_WIDTH, 50.0f, 5.0f+GAME_LIST_WIDTH+5, 50.0f, 5.0f+GAME_LIST_WIDTH, 312.0f, 5.0f+GAME_LIST_WIDTH+5, 312.0f, 4, GS_GREY);

	/* Draw the bottom of the table... */
	gsKit_prim_line(gsGlobal, 9.0f, 311.0f, 5.0f+GAME_LIST_WIDTH, 311.0f, 4, GS_WHITE);
	gsKit_prim_quad(gsGlobal, 5.0f, 312.0f, 5.0f, 315.0f, 5.0f+GAME_LIST_WIDTH+5, 312.0f, 5.0f+GAME_LIST_WIDTH+5, 315.0f, 4, GS_GREY);

#ifndef UI_TEST_MODE
	NumGamesToDisplay=(NumHDLGames<GAME_LIST_MAX_DISPLAYED_GAMES)?NumHDLGames:GAME_LIST_MAX_DISPLAYED_GAMES;

	if(LastSelectedItem != SelectedGameListItem)
	{
		frames = 0;
		PausedFrames = 0;
		LastSelectedItem = SelectedGameListItem;
	}

	/* Draw the game list */
	for(i=0; i<NumGamesToDisplay; i++)
	{
		mbstowcs(title, HDLGameList[GameListViewPortStart+i].GameTitle, GAME_TITLE_MAX_LEN+1);
		if(i == SelectedGameListItem)
		{
			if(frames > GAME_LIST_TITLE_SCROLL_START_PAUSE_INTERVAL)
				ScrollOffset = (frames - GAME_LIST_TITLE_SCROLL_START_PAUSE_INTERVAL) / GAME_LIST_TITLE_SCROLL_INTERVAL;
			else
				ScrollOffset = 0;

			len = wcslen(title);
			//Scroll if necessary.
			if(wFontPrintField(gsGlobal, 14, 60+i*16, 1, 1.0f, GS_WHITE_FONT, &title[ScrollOffset], GAME_LIST_TITLE_MAX_PIX, -1) < len - ScrollOffset)
			{
				frames++;
			}
			else
			{	//At the very end of the scrolling, pause.
				PausedFrames++;
				if(PausedFrames >= GAME_LIST_TITLE_SCROLL_END_PAUSE_INTERVAL)
				{	//When paused for long enough, start scrolling again from the start.
					frames = 0;
					PausedFrames = 0;
				}
			}
		} else {
			//Line is not selected. Print the title, but truncate if necessary.
			wFontPrintTitle(gsGlobal, 14, 60+i*16, 1, 1.0f, GS_WHITE_FONT, title, GAME_LIST_TITLE_MAX_PIX);
		}
	}
#else
	NumGamesToDisplay=GAME_LIST_MAX_DISPLAYED_GAMES;

	for(i=0; i<NumGamesToDisplay; i++){
		FontPrintf(gsGlobal, 14, 60+i*16, 1, 1.0f, GS_WHITE_FONT, "Game title here");
//		FontPrintf(gsGlobal, 14, 60+i*16, 1, 1.0f, GS_WHITE_FONT, "111111111122222222223333333333444444444455555555556666666666777777777788888888889999999999");
	}
#endif

	/* Draw the highlight. */
	gsKit_prim_quad(gsGlobal, 11, 60+SelectedGameListItem*16, 11, 60+SelectedGameListItem*16+16, GAME_LIST_WIDTH, 60+SelectedGameListItem*16, GAME_LIST_WIDTH, 60+SelectedGameListItem*16+16, 8, GS_LBLUE_TRANS);

	/* Draw the button legend. */
	DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_UD_DPAD, 10, 330, 4);
	FontPrintf(gsGlobal, 50, 330, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_SELECT_GAME));
	DrawButtonLegend(gsGlobal, &PadLayoutTexture, SelectButton == PAD_CIRCLE ? BUTTON_TYPE_CIRCLE : BUTTON_TYPE_CROSS, 300, 330, 4);
	FontPrintf(gsGlobal, 340, 330, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_GAME_OPTIONS));
	DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_TRIANGLE, 10, 360, 4);
	FontPrintf(gsGlobal, 50, 360, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_DELETE_GAME));
	DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_START, 300, 360, 4);
	FontPrintf(gsGlobal, 340, 360, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_INSTALL_GAME));
	DrawButtonLegend(gsGlobal, &PadLayoutTexture, CancelButton == PAD_CIRCLE ? BUTTON_TYPE_CIRCLE : BUTTON_TYPE_CROSS, 10, 390, 4);
	FontPrintf(gsGlobal, 50, 390, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_QUIT));
	DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_SELECT, 300, 390, 4);
	FontPrintf(gsGlobal, 340, 390, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_OPTIONS));
}

static unsigned int GetGameListData(struct HDLGameEntry **HDLGameList, struct GameListDisplayData *GameListDisplayData){
	unsigned int NumGames;

	NumGames=GetHDLGameList(HDLGameList);
	GameListDisplayData->SelectedGameIndex=GameListDisplayData->GameListViewPortStart=0;

	return NumGames;
}

static unsigned int LoadGameList(struct HDLGameEntry **HDLGameList, struct GameListDisplayData *GameListDisplayData){
	unsigned int result;

	result = GetGameListData(HDLGameList, GameListDisplayData);

	sysGetFreeDiskSpaceDisplay(FreeDiskSpaceDisplay);

	return result;
}

static unsigned int ReloadGameList(struct HDLGameEntry **HDLGameList, struct GameListDisplayData *GameListDisplayData){
	unsigned int result;

	DisplayFlashStatusUpdate(SYS_UI_MSG_PLEASE_WAIT);
	LoadHDLGameList(HDLGameList);
	return LoadGameList(HDLGameList, GameListDisplayData);
}

static void DeleteGame(struct HDLGameEntry *HDLGameList, unsigned int GameIndex){
#ifndef UI_TEST_MODE
	char *path;

	path=malloc(strlen(HDLGameList[GameIndex].PartName)+6);	/* Length of "hdd0:" + length of partition name + 1 NULL terminator. */
	sprintf(path, "hdd0:%s", HDLGameList[GameIndex].PartName);
	RemoveGameInstallation(path);
	free(path);
#endif
}

static int UpdateGame(struct HDLGameEntry *HDLGameList, unsigned int GameIndex){
	int result, IconSource;
	struct GameSettings GameSettings;
	char *ExternalIconSourcePath;

#ifndef UI_TEST_MODE
	struct IconSysData IconSys;
	mcIcon McSaveIconSys;
	char SaveFolderPath[38];

	GameSettings.UseMDMA0=(HDLGameList[GameIndex].TRType==ATA_XFER_MODE_MDMA && HDLGameList[GameIndex].TRMode==0)?1:0;
	mbstowcs(GameSettings.FullTitle, HDLGameList[GameIndex].GameTitle, GAME_TITLE_MAX_LEN+1);
	GameSettings.FullTitle[GAME_TITLE_MAX_LEN]='\0';
	GetGameInstallationOSDIconSys(HDLGameList[GameIndex].PartName, &IconSys);
	wcsncpy(GameSettings.OSDTitleLine1, IconSys.title0, OSD_TITLE_MAX_LEN);
	GameSettings.OSDTitleLine1[OSD_TITLE_MAX_LEN]='\0';
	wcsncpy(GameSettings.OSDTitleLine2, IconSys.title1, OSD_TITLE_MAX_LEN);
	GameSettings.OSDTitleLine2[OSD_TITLE_MAX_LEN]='\0';

	GameSettings.CompatibilityModeFlags=HDLGameList[GameIndex].CompatibilityModeFlags;

RedisplayGameOptionScreen:
	if((result=GetUserGameSettings(&GameSettings))==0){
		ExternalIconSourcePath=NULL;
RedisplayGameIconOptionScreen:
		if(ExternalIconSourcePath!=NULL){
			free(ExternalIconSourcePath);
			ExternalIconSourcePath=NULL;
		}
		if(DisplayPromptMessage(SYS_UI_MSG_CHANGE_ICON, SYS_UI_LBL_NO, SYS_UI_LBL_YES)==2){
			if((IconSource=GetUserIconSourceChoice(&ExternalIconSourcePath))<0){
				goto RedisplayGameOptionScreen;
			}
			else{
				if(IconSource!=ICON_SOURCE_DEFAULT){
					if(IconSource==ICON_SOURCE_SAVE){
						result=LoadMcSaveSys(SaveFolderPath, &McSaveIconSys, HDLGameList[GameIndex].DiscID);
					}
					else{
						result=LoadMcSaveSysFromPath(ExternalIconSourcePath, &McSaveIconSys);
					}

					if(result<0){
						if(DisplayPromptMessage(SYS_UI_MSG_ICON_LOAD_FAIL, SYS_UI_LBL_NO, SYS_UI_LBL_YES)==2){
							IconSource=ICON_SOURCE_DEFAULT;
						}
						else{
							goto RedisplayGameIconOptionScreen;
						}
					}
				}
			}
		}
		else IconSource=-1;

		if(GameSettings.UseMDMA0){
			/* Use MDMA Mode 0 */
			HDLGameList[GameIndex].TRType=ATA_XFER_MODE_MDMA;
			HDLGameList[GameIndex].TRMode=0;
		}
		else{
			/* Use UDMA Mode 4 */
			HDLGameList[GameIndex].TRType=ATA_XFER_MODE_UDMA;
			HDLGameList[GameIndex].TRMode=4;
		}

		if((result=UpdateGameInstallation(HDLGameList[GameIndex].PartName, GameSettings.FullTitle, GameSettings.CompatibilityModeFlags, HDLGameList[GameIndex].TRType, HDLGameList[GameIndex].TRMode, HDLGameList[GameIndex].DiscType))>=0){
			if(IconSource<0){
				if((result=UpdateGameInstallationOSDResources(HDLGameList[GameIndex].PartName, GameSettings.OSDTitleLine1, GameSettings.OSDTitleLine2))!=0){
					DEBUG_PRINTF("UpdateGame: Can't update OSD resources: %d\n", result);
				}
			}
			else{
				//Change the icon.
InstallOSDResources_start:
				if((result=InstallGameInstallationOSDResources(HDLGameList[GameIndex].PartName, &GameSettings, IconSource!=ICON_SOURCE_DEFAULT?&McSaveIconSys:NULL, IconSource!=ICON_SOURCE_DEFAULT?(IconSource==ICON_SOURCE_EXTERNAL?ExternalIconSourcePath:SaveFolderPath):NULL))>=0){
					if(IconSource==ICON_SOURCE_SAVE || IconSource==ICON_SOURCE_EXTERNAL){
						if(result!=ICON_SOURCE_SAVE){
							if(DisplayPromptMessage(SYS_UI_MSG_ICON_LOAD_FAIL, SYS_UI_LBL_NO, SYS_UI_LBL_YES)==2){
								IconSource=ICON_SOURCE_DEFAULT;
								goto InstallOSDResources_start;
							}
							else{
								goto RedisplayGameIconOptionScreen;
							}
						}
					}

					if(result>=0) DisplayInfoMessage(SYS_UI_MSG_GAME_UPDATE_COMPLETE);
				}
				else{
					DEBUG_PRINTF("Error installing OSD files: %d\n", result);
					DisplayErrorMessage(SYS_UI_MSG_OSD_INST_FAILED);
				}
			}
		}
		else{
			DEBUG_PRINTF("UpdateGame: Can't update game: %d\n", result);
		}

		if(ExternalIconSourcePath!=NULL) free(ExternalIconSourcePath);
		if(result>=0) result=0;
	}
#else
	GameSettings.UseMDMA0=0;
	GameSettings.CompatibilityModeFlags=0;
	wcscpy(GameSettings.FullTitle, L"FULL TITLE HERE");
	wcscpy(GameSettings.OSDTitleLine1, L"TITLE LINE 1 HERE");
	wcscpy(GameSettings.OSDTitleLine2, L"TITLE LINE 2 HERE");
RedisplayGameOptionScreen:
	if((result=GetUserGameSettings(&GameSettings))==0){
		if(DisplayPromptMessage(SYS_UI_MSG_CHANGE_ICON, SYS_UI_LBL_NO, SYS_UI_LBL_YES)==2){
			if((IconSource=GetUserIconSourceChoice(&ExternalIconSourcePath))<0){
				goto RedisplayGameOptionScreen;
			}

			if(ExternalIconSourcePath!=NULL) free(ExternalIconSourcePath);
		}
	}
#endif

	return result;
}

enum NETSTAT_SCREEN_ID{
	NETSTAT_ID_ADDRESS_TYPE	= 1,
	NETSTAT_ID_MAC_0,
	NETSTAT_ID_MAC_1,
	NETSTAT_ID_MAC_2,
	NETSTAT_ID_MAC_3,
	NETSTAT_ID_MAC_4,
	NETSTAT_ID_MAC_5,
	NETSTAT_ID_IP_0,
	NETSTAT_ID_IP_1,
	NETSTAT_ID_IP_2,
	NETSTAT_ID_IP_3,
	NETSTAT_ID_NM_0,
	NETSTAT_ID_NM_1,
	NETSTAT_ID_NM_2,
	NETSTAT_ID_NM_3,
	NETSTAT_ID_GW_0,
	NETSTAT_ID_GW_1,
	NETSTAT_ID_GW_2,
	NETSTAT_ID_GW_3,
	NETSTAT_ID_LINK_STATE,
	NETSTAT_ID_LINK_MODE_LABEL,
	NETSTAT_ID_LINK_MODE_TAB,
	NETSTAT_ID_LINK_MODE,
	NETSTAT_ID_LINK_FLOW_CONTROL_LABEL,
	NETSTAT_ID_LINK_FLOW_CONTROL_TAB,
	NETSTAT_ID_LINK_FLOW_CONTROL,
	NETSTAT_ID_DROPPED_TX_FRAMES,
	NETSTAT_ID_DROPPED_RX_FRAMES,
	NETSTAT_ID_TX_LOSSCR,
	NETSTAT_ID_RX_OVERRUN,
	NETSTAT_ID_TX_EDEFER,
	NETSTAT_ID_RX_BADLEN,
	NETSTAT_ID_TX_COLLISION,
	NETSTAT_ID_RX_BADFCS,
	NETSTAT_ID_TX_UNDERRUN,
	NETSTAT_ID_RX_BADALIGN
};

static struct UIMenuItem NetstatMenuItems[]={
	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_NETWORK_STATUS },
	{ MITEM_SEPERATOR },
	{ MITEM_BREAK },

	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_ADDRESS_TYPE }, { MITEM_TAB }, { MITEM_LABEL, NETSTAT_ID_ADDRESS_TYPE }, { MITEM_BREAK },
	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_MAC_ADDRESS }, { MITEM_TAB },
		{ MITEM_VALUE, NETSTAT_ID_MAC_0, MITEM_FLAG_READONLY, MITEM_FORMAT_HEX, 2, 0, 0, 0, 0, 255 }, { MITEM_COLON },
		{ MITEM_VALUE, NETSTAT_ID_MAC_1, MITEM_FLAG_READONLY, MITEM_FORMAT_HEX, 2, 0, 0, 0, 0, 255 }, { MITEM_COLON },
		{ MITEM_VALUE, NETSTAT_ID_MAC_2, MITEM_FLAG_READONLY, MITEM_FORMAT_HEX, 2, 0, 0, 0, 0, 255 }, { MITEM_COLON },
		{ MITEM_VALUE, NETSTAT_ID_MAC_3, MITEM_FLAG_READONLY, MITEM_FORMAT_HEX, 2, 0, 0, 0, 0, 255 }, { MITEM_COLON },
		{ MITEM_VALUE, NETSTAT_ID_MAC_4, MITEM_FLAG_READONLY, MITEM_FORMAT_HEX, 2, 0, 0, 0, 0, 255 }, { MITEM_COLON },
		{ MITEM_VALUE, NETSTAT_ID_MAC_5, MITEM_FLAG_READONLY, MITEM_FORMAT_HEX, 2, 0, 0, 0, 0, 255 }, { MITEM_BREAK },
	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_IP_ADDRESS }, { MITEM_TAB },
		{ MITEM_VALUE, NETSTAT_ID_IP_0, MITEM_FLAG_READONLY, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_DOT },
		{ MITEM_VALUE, NETSTAT_ID_IP_1, MITEM_FLAG_READONLY, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_DOT },
		{ MITEM_VALUE, NETSTAT_ID_IP_2, MITEM_FLAG_READONLY, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_DOT },
		{ MITEM_VALUE, NETSTAT_ID_IP_3, MITEM_FLAG_READONLY, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_BREAK },
	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_NM_ADDRESS }, { MITEM_TAB },
		{ MITEM_VALUE, NETSTAT_ID_NM_0, MITEM_FLAG_READONLY, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_DOT },
		{ MITEM_VALUE, NETSTAT_ID_NM_1, MITEM_FLAG_READONLY, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_DOT },
		{ MITEM_VALUE, NETSTAT_ID_NM_2, MITEM_FLAG_READONLY, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_DOT },
		{ MITEM_VALUE, NETSTAT_ID_NM_3, MITEM_FLAG_READONLY, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_BREAK },
	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_GW_ADDRESS }, { MITEM_TAB },
		{ MITEM_VALUE, NETSTAT_ID_GW_0, MITEM_FLAG_READONLY, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_DOT },
		{ MITEM_VALUE, NETSTAT_ID_GW_1, MITEM_FLAG_READONLY, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_DOT },
		{ MITEM_VALUE, NETSTAT_ID_GW_2, MITEM_FLAG_READONLY, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_DOT },
		{ MITEM_VALUE, NETSTAT_ID_GW_3, MITEM_FLAG_READONLY, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_BREAK },

	{ MITEM_BREAK },

	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_LINK_STATE }, { MITEM_TAB }, { MITEM_LABEL, NETSTAT_ID_LINK_STATE }, { MITEM_BREAK },
	{ MITEM_LABEL, NETSTAT_ID_LINK_MODE_LABEL, 0, 0, 0, 0, 0, SYS_UI_LBL_LINK_MODE }, { MITEM_TAB, NETSTAT_ID_LINK_MODE_TAB }, { MITEM_LABEL, NETSTAT_ID_LINK_MODE }, { MITEM_BREAK },
	{ MITEM_LABEL, NETSTAT_ID_LINK_FLOW_CONTROL_LABEL, 0, 0, 0, 0, 0, SYS_UI_LBL_FLOW_CONTROL }, { MITEM_TAB, NETSTAT_ID_LINK_FLOW_CONTROL_TAB }, { MITEM_LABEL, NETSTAT_ID_LINK_FLOW_CONTROL }, { MITEM_BREAK },

	{ MITEM_BREAK },

	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_DROPPED_TX_FRAMES }, { MITEM_TAB }, { MITEM_VALUE, NETSTAT_ID_DROPPED_TX_FRAMES, MITEM_FLAG_READONLY }, { MITEM_BREAK },
	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_DROPPED_RX_FRAMES }, { MITEM_TAB }, { MITEM_VALUE, NETSTAT_ID_DROPPED_RX_FRAMES, MITEM_FLAG_READONLY }, { MITEM_BREAK },

	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_TX_LOSSCR }, { MITEM_TAB }, { MITEM_VALUE, NETSTAT_ID_TX_LOSSCR, MITEM_FLAG_READONLY }, { MITEM_TAB },
	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_RX_OVERRUN }, { MITEM_TAB }, { MITEM_TAB }, { MITEM_VALUE, NETSTAT_ID_RX_OVERRUN, MITEM_FLAG_READONLY }, { MITEM_BREAK },
	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_TX_EDEFER }, { MITEM_TAB }, { MITEM_VALUE, NETSTAT_ID_TX_EDEFER, MITEM_FLAG_READONLY }, { MITEM_TAB },
	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_RX_BADLEN }, { MITEM_TAB }, { MITEM_TAB }, { MITEM_VALUE, NETSTAT_ID_RX_BADLEN, MITEM_FLAG_READONLY }, { MITEM_BREAK },
	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_TX_COLLISON }, { MITEM_TAB }, { MITEM_VALUE, NETSTAT_ID_TX_COLLISION, MITEM_FLAG_READONLY }, { MITEM_TAB },
	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_RX_BADFCS }, { MITEM_TAB }, { MITEM_TAB }, { MITEM_VALUE, NETSTAT_ID_RX_BADFCS, MITEM_FLAG_READONLY }, { MITEM_BREAK },
	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_TX_UNDERRUN }, { MITEM_TAB }, { MITEM_VALUE, NETSTAT_ID_TX_UNDERRUN, MITEM_FLAG_READONLY }, { MITEM_TAB },
	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_RX_BADALIGN }, { MITEM_TAB }, { MITEM_VALUE, NETSTAT_ID_RX_BADALIGN, MITEM_FLAG_READONLY }, { MITEM_BREAK },

	{MITEM_TERMINATOR}
};

static struct UIMenu NetstatMenu = {NULL, NULL, NetstatMenuItems, {{-1, -1}, {BUTTON_TYPE_SYS_CANCEL, SYS_UI_LBL_BACK}}};

static void UpdateNetworkStatus(void)
{
	u8 ip_address[4], subnet_mask[4], gateway[4];
	u8 dhcp;
#ifdef ENABLE_NETWORK_SUPPORT
	t_ip_info ip_info;
#endif

#ifdef ENABLE_NETWORK_SUPPORT
	//SMAP is registered as the "sm0" device to the TCP/IP stack.
	if (ps2ip_getconfig("sm0", &ip_info) >= 0)
	{
		ip_address[0] = ip4_addr1((struct ip4_addr *)&ip_info.ipaddr);
		ip_address[1] = ip4_addr2((struct ip4_addr *)&ip_info.ipaddr);
		ip_address[2] = ip4_addr3((struct ip4_addr *)&ip_info.ipaddr);
		ip_address[3] = ip4_addr4((struct ip4_addr *)&ip_info.ipaddr);

		subnet_mask[0] = ip4_addr1((struct ip4_addr *)&ip_info.netmask);
		subnet_mask[1] = ip4_addr2((struct ip4_addr *)&ip_info.netmask);
		subnet_mask[2] = ip4_addr3((struct ip4_addr *)&ip_info.netmask);
		subnet_mask[3] = ip4_addr4((struct ip4_addr *)&ip_info.netmask);

		gateway[0] = ip4_addr1((struct ip4_addr *)&ip_info.gw);
		gateway[1] = ip4_addr2((struct ip4_addr *)&ip_info.gw);
		gateway[2] = ip4_addr3((struct ip4_addr *)&ip_info.gw);
		gateway[3] = ip4_addr4((struct ip4_addr *)&ip_info.gw);

		dhcp = ip_info.dhcp_enabled;
	} else {
#endif
		memset(ip_address, 0, sizeof(ip_address));
		memset(subnet_mask, 0, sizeof(subnet_mask));
		memset(gateway, 0, sizeof(gateway));
		dhcp = 0;
#ifdef ENABLE_NETWORK_SUPPORT
	}
#endif

	UISetLabel(&NetstatMenu, NETSTAT_ID_ADDRESS_TYPE, dhcp ? SYS_UI_LBL_IP_DHCP : SYS_UI_LBL_IP_STATIC);

	UISetValue(&NetstatMenu, NETSTAT_ID_IP_0, ip_address[0]);
	UISetValue(&NetstatMenu, NETSTAT_ID_IP_1, ip_address[1]);
	UISetValue(&NetstatMenu, NETSTAT_ID_IP_2, ip_address[2]);
	UISetValue(&NetstatMenu, NETSTAT_ID_IP_3, ip_address[3]);
	UISetValue(&NetstatMenu, NETSTAT_ID_NM_0, subnet_mask[0]);
	UISetValue(&NetstatMenu, NETSTAT_ID_NM_1, subnet_mask[1]);
	UISetValue(&NetstatMenu, NETSTAT_ID_NM_2, subnet_mask[2]);
	UISetValue(&NetstatMenu, NETSTAT_ID_NM_3, subnet_mask[3]);
	UISetValue(&NetstatMenu, NETSTAT_ID_GW_0, gateway[0]);
	UISetValue(&NetstatMenu, NETSTAT_ID_GW_1, gateway[1]);
	UISetValue(&NetstatMenu, NETSTAT_ID_GW_2, gateway[2]);
	UISetValue(&NetstatMenu, NETSTAT_ID_GW_3, gateway[3]);
}

static void UpdateHardwareAddress(void)
{
	memset(mac_address, 0, sizeof(mac_address));
	NetManIoctl(NETMAN_NETIF_IOCTL_ETH_GET_MAC, NULL, 0, mac_address, sizeof(mac_address));

	UISetValue(&NetstatMenu, NETSTAT_ID_MAC_0, mac_address[0]);
	UISetValue(&NetstatMenu, NETSTAT_ID_MAC_1, mac_address[1]);
	UISetValue(&NetstatMenu, NETSTAT_ID_MAC_2, mac_address[2]);
	UISetValue(&NetstatMenu, NETSTAT_ID_MAC_3, mac_address[3]);
	UISetValue(&NetstatMenu, NETSTAT_ID_MAC_4, mac_address[4]);
	UISetValue(&NetstatMenu, NETSTAT_ID_MAC_5, mac_address[5]);
}

static int NetstatMenuUpdate(struct UIMenu *menu, unsigned short int frame, int selection, u32 padstatus)
{
	int result, NetworkLinkState, NetworkLinkMode, NetworkLinkFlowControl;
	int RxDroppedFrameCount, RxFrameOverrunCount, RxFrameBadLengthCount, RxFrameBadFCSCount, RxFrameBadAlignmentCount, TxDroppedFrameCount, TxFrameLOSSCRCount, TxFrameEDEFERCount, TxFrameCollisionCount, TxFrameUnderrunCount;
	const int NetworkLinkModeLabels[NETMAN_NETIF_ETH_LINK_MODE_COUNT]={
		SYS_UI_LBL_UNKNOWN,		//AUTO setting, but won't be used.
		SYS_UI_LBL_MODE_10MBIT_HDX,
		SYS_UI_LBL_MODE_10MBIT_FDX,
		SYS_UI_LBL_MODE_100MBIT_HDX,
		SYS_UI_LBL_MODE_100MBIT_FDX,
		SYS_UI_LBL_UNKNOWN
	};

	if(frame % UPDATE_INTERVAL == 0)
	{
		NetworkLinkState = NetManIoctl(NETMAN_NETIF_IOCTL_GET_LINK_STATUS, NULL, 0, NULL, 0);

		UISetLabel(&NetstatMenu, NETSTAT_ID_LINK_STATE, (NetworkLinkState==NETMAN_NETIF_ETH_LINK_STATE_UP)?SYS_UI_LBL_UP:((NetworkLinkState==NETMAN_NETIF_ETH_LINK_STATE_DOWN)?SYS_UI_LBL_DOWN:SYS_UI_LBL_UNKNOWN));

		if(NetworkLinkState == NETMAN_NETIF_ETH_LINK_STATE_UP)
		{
			result = NetManIoctl(NETMAN_NETIF_IOCTL_ETH_GET_LINK_MODE, NULL, 0, NULL, 0);
			NetworkLinkMode = result & ~NETMAN_NETIF_ETH_LINK_DISABLE_PAUSE;
			NetworkLinkFlowControl = (result & NETMAN_NETIF_ETH_LINK_DISABLE_PAUSE) == 0;

			TxDroppedFrameCount = NetManIoctl(NETMAN_NETIF_IOCTL_GET_TX_DROPPED_COUNT, NULL, 0, NULL, 0);
			RxDroppedFrameCount = NetManIoctl(NETMAN_NETIF_IOCTL_GET_RX_DROPPED_COUNT, NULL, 0, NULL, 0);
			RxFrameOverrunCount = NetManIoctl(NETMAN_NETIF_IOCTL_ETH_GET_RX_EOVERRUN_CNT, NULL, 0, NULL, 0);
			RxFrameBadLengthCount = NetManIoctl(NETMAN_NETIF_IOCTL_ETH_GET_RX_EBADLEN_CNT, NULL, 0, NULL, 0);
			RxFrameBadFCSCount = NetManIoctl(NETMAN_NETIF_IOCTL_ETH_GET_RX_EBADFCS_CNT, NULL, 0, NULL, 0);
			RxFrameBadAlignmentCount = NetManIoctl(NETMAN_NETIF_IOCTL_ETH_GET_RX_EBADALIGN_CNT, NULL, 0, NULL, 0);
			TxFrameLOSSCRCount = NetManIoctl(NETMAN_NETIF_IOCTL_ETH_GET_TX_ELOSSCR_CNT, NULL, 0, NULL, 0);
			TxFrameEDEFERCount = NetManIoctl(NETMAN_NETIF_IOCTL_ETH_GET_TX_EEDEFER_CNT, NULL, 0, NULL, 0);
			TxFrameCollisionCount = NetManIoctl(NETMAN_NETIF_IOCTL_ETH_GET_TX_ECOLL_CNT, NULL, 0, NULL, 0);
			TxFrameUnderrunCount = NetManIoctl(NETMAN_NETIF_IOCTL_ETH_GET_TX_EUNDERRUN_CNT, NULL, 0, NULL, 0);

			UISetLabel(&NetstatMenu, NETSTAT_ID_LINK_MODE, (NetworkLinkMode>=0 && NetworkLinkMode<NETMAN_NETIF_ETH_LINK_MODE_COUNT)?NetworkLinkModeLabels[NetworkLinkMode]:SYS_UI_LBL_UNKNOWN);
			UISetLabel(&NetstatMenu, NETSTAT_ID_LINK_FLOW_CONTROL, NetworkLinkFlowControl ? SYS_UI_LBL_ENABLED : SYS_UI_LBL_DISABLED);
			UISetVisible(&NetstatMenu, NETSTAT_ID_LINK_MODE_LABEL, 1);
			UISetVisible(&NetstatMenu, NETSTAT_ID_LINK_MODE_TAB, 1);
			UISetVisible(&NetstatMenu, NETSTAT_ID_LINK_MODE, 1);
			UISetVisible(&NetstatMenu, NETSTAT_ID_LINK_FLOW_CONTROL_LABEL, 1);
			UISetVisible(&NetstatMenu, NETSTAT_ID_LINK_FLOW_CONTROL_TAB, 1);
			UISetVisible(&NetstatMenu, NETSTAT_ID_LINK_FLOW_CONTROL, 1);
			UISetValue(&NetstatMenu, NETSTAT_ID_DROPPED_TX_FRAMES, TxDroppedFrameCount);
			UISetValue(&NetstatMenu, NETSTAT_ID_DROPPED_RX_FRAMES, RxDroppedFrameCount);
			UISetValue(&NetstatMenu, NETSTAT_ID_RX_OVERRUN, RxFrameOverrunCount);
			UISetValue(&NetstatMenu, NETSTAT_ID_RX_BADLEN, RxFrameBadLengthCount);
			UISetValue(&NetstatMenu, NETSTAT_ID_RX_BADFCS, RxFrameBadFCSCount);
			UISetValue(&NetstatMenu, NETSTAT_ID_RX_BADALIGN, RxFrameBadAlignmentCount);
			UISetValue(&NetstatMenu, NETSTAT_ID_TX_LOSSCR, TxFrameLOSSCRCount);
			UISetValue(&NetstatMenu, NETSTAT_ID_TX_EDEFER, TxFrameEDEFERCount);
			UISetValue(&NetstatMenu, NETSTAT_ID_TX_COLLISION, TxFrameCollisionCount);
			UISetValue(&NetstatMenu, NETSTAT_ID_TX_UNDERRUN, TxFrameUnderrunCount);
		} else {
			UISetVisible(&NetstatMenu, NETSTAT_ID_LINK_MODE_LABEL, 0);
			UISetVisible(&NetstatMenu, NETSTAT_ID_LINK_MODE_TAB, 0);
			UISetVisible(&NetstatMenu, NETSTAT_ID_LINK_MODE, 0);
			UISetVisible(&NetstatMenu, NETSTAT_ID_LINK_FLOW_CONTROL_LABEL, 0);
			UISetVisible(&NetstatMenu, NETSTAT_ID_LINK_FLOW_CONTROL_TAB, 0);
			UISetVisible(&NetstatMenu, NETSTAT_ID_LINK_FLOW_CONTROL, 0);
		}

		UpdateNetworkStatus();
	}

	return 0;
}

static void ShowNetworkStatus(void)
{
	//Show back button on network status screen.
	NetstatMenu.hints[1].button = BUTTON_TYPE_SYS_CANCEL;

	UIExecMenu(&NetstatMenu, -1, NULL, &NetstatMenuUpdate);
}

enum REMOCON_SCREEN_ID{
	REMOCON_SCREEN_ID_TITLE	= 1,
	REMOCON_SCREEN_ID_MESSAGE
};

static struct UIMenuItem RemoconMenuItems[]={
	{MITEM_LABEL, REMOCON_SCREEN_ID_TITLE, 0, 0, 0, 0, 0, SYS_UI_LBL_REMOTE_CONN},
	{MITEM_SEPERATOR},
	{MITEM_BREAK},

	{MITEM_STRING, REMOCON_SCREEN_ID_MESSAGE, MITEM_FLAG_READONLY}, {MITEM_BREAK}, {MITEM_BREAK},

	{MITEM_TERMINATOR}
};

static struct UIMenu RemoconMenu = {NULL, NULL, RemoconMenuItems, {{-1, -1}, {-1, -1}}};

static void EnterRemoteClientMenu(struct RuntimeData *RuntimeData){
	unsigned int PadStatus;
	unsigned int frame;
	unsigned char menuToShow;

	//Hide back button on network status screen.
	NetstatMenu.hints[1].button = -1;
	//Set on-screen message.
	UISetString(&RemoconMenu, REMOCON_SCREEN_ID_MESSAGE, GetUIString(SYS_UI_MSG_REMOTE_CONN));

	//End communications with PADMAN to improve response time by the IOP, as PADMAN sends updates for the pad status every 1/60th a second.
	DeinitPads();

	frame = 0;
	menuToShow = 0;
	while(RuntimeData->IsRemoteClientConnected){
		UIDrawMenu((menuToShow == 0) ? &RemoconMenu : &NetstatMenu, 0, UI_OFFSET_X, UI_OFFSET_Y, -1);

		SyncFlipFB();
		frame++;

		if (frame % NETSTAT_UPDATE_INTERVAL == 0)
			menuToShow ^= 1;

		//Update the network status menu when it is shown.
		if (menuToShow == 1)
			NetstatMenuUpdate(&NetstatMenu, frame, 0, 0);

		PadStatus=ReadCombinedPadStatus();
	}

	//Re-enable communications with PADMAN.
	InitPads();
}

enum OPTIONS_SCREEN_ID{
	OPTIONS_ID_SORT_TITLES	= 1,
	OPTIONS_ID_NETWORK_DHCP,
	OPTIONS_ID_IP_0,
	OPTIONS_ID_IP_1,
	OPTIONS_ID_IP_2,
	OPTIONS_ID_IP_3,
	OPTIONS_ID_NM_0,
	OPTIONS_ID_NM_1,
	OPTIONS_ID_NM_2,
	OPTIONS_ID_NM_3,
	OPTIONS_ID_GW_0,
	OPTIONS_ID_GW_1,
	OPTIONS_ID_GW_2,
	OPTIONS_ID_GW_3,
	OPTIONS_ID_ADVANCED_NETWORK,
	OPTIONS_ID_LINK_MODE,
	OPTIONS_ID_LINK_FLOW_CONTROL,

	OPTIONS_ID_OK
};

static struct UIMenuItem OptionsMenuItems[]={
	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_OPTIONS },
	{ MITEM_SEPERATOR },
	{ MITEM_BREAK },

	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_SORT_TITLES }, { MITEM_TAB }, { MITEM_TOGGLE, OPTIONS_ID_SORT_TITLES }, { MITEM_BREAK },

	{ MITEM_BREAK },
	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_NETWORK_OPTIONS },
	{ MITEM_SEPERATOR },
	{ MITEM_BREAK },

	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_USE_DHCP }, { MITEM_TAB }, { MITEM_TOGGLE, OPTIONS_ID_NETWORK_DHCP }, { MITEM_BREAK },
	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_IP_ADDRESS }, { MITEM_TAB },
		{ MITEM_VALUE, OPTIONS_ID_IP_0, 0, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_DOT },
		{ MITEM_VALUE, OPTIONS_ID_IP_1, 0, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_DOT },
		{ MITEM_VALUE, OPTIONS_ID_IP_2, 0, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_DOT },
		{ MITEM_VALUE, OPTIONS_ID_IP_3, 0, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_BREAK },
	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_NM_ADDRESS }, { MITEM_TAB },
		{ MITEM_VALUE, OPTIONS_ID_NM_0, 0, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_DOT },
		{ MITEM_VALUE, OPTIONS_ID_NM_1, 0, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_DOT },
		{ MITEM_VALUE, OPTIONS_ID_NM_2, 0, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_DOT },
		{ MITEM_VALUE, OPTIONS_ID_NM_3, 0, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_BREAK },
	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_GW_ADDRESS }, { MITEM_TAB },
		{ MITEM_VALUE, OPTIONS_ID_GW_0, 0, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_DOT },
		{ MITEM_VALUE, OPTIONS_ID_GW_1, 0, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_DOT },
		{ MITEM_VALUE, OPTIONS_ID_GW_2, 0, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_DOT },
		{ MITEM_VALUE, OPTIONS_ID_GW_3, 0, MITEM_FORMAT_UDEC, 3, 0, 0, 0, 0, 255 }, { MITEM_BREAK },

	{ MITEM_BREAK },
	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_ADVANCED_SETTINGS }, { MITEM_TAB }, { MITEM_TOGGLE, OPTIONS_ID_ADVANCED_NETWORK }, { MITEM_BREAK },
	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_LINK_MODE }, { MITEM_TAB }, { MITEM_TAB }, { MITEM_ENUM, OPTIONS_ID_LINK_MODE }, { MITEM_BREAK },
	{ MITEM_LABEL, 0, 0, 0, 0, 0, 0, SYS_UI_LBL_FLOW_CONTROL }, { MITEM_TAB }, { MITEM_TAB }, { MITEM_TOGGLE, OPTIONS_ID_LINK_FLOW_CONTROL }, { MITEM_BREAK },

	{ MITEM_BREAK },

	{MITEM_BUTTON, OPTIONS_ID_OK, 0, 0, 16}, {MITEM_BREAK}, {MITEM_BREAK},

	{MITEM_TERMINATOR}
};

static struct UIMenu OptionsMenu = {NULL, NULL, OptionsMenuItems, {{BUTTON_TYPE_SYS_SELECT, SYS_UI_LBL_OK}, {BUTTON_TYPE_SYS_CANCEL, SYS_UI_LBL_CANCEL}}};

static int OptionsMenuUpdate(struct UIMenu *menu, unsigned short int frame, int selection, u32 padstatus)
{
	int result, ShowAdvancedOptions, UseDHCP;

	UseDHCP = UIGetValue(&OptionsMenu, OPTIONS_ID_NETWORK_DHCP);

	if(!UseDHCP)
	{
		UISetEnabled(&OptionsMenu, OPTIONS_ID_IP_0, 1);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_IP_1, 1);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_IP_2, 1);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_IP_3, 1);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_NM_0, 1);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_NM_1, 1);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_NM_2, 1);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_NM_3, 1);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_GW_0, 1);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_GW_1, 1);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_GW_2, 1);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_GW_3, 1);
	} else {
		UISetEnabled(&OptionsMenu, OPTIONS_ID_IP_0, 0);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_IP_1, 0);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_IP_2, 0);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_IP_3, 0);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_NM_0, 0);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_NM_1, 0);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_NM_2, 0);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_NM_3, 0);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_GW_0, 0);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_GW_1, 0);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_GW_2, 0);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_GW_3, 0);
	}

	ShowAdvancedOptions = UIGetValue(&OptionsMenu, OPTIONS_ID_ADVANCED_NETWORK);

	if(ShowAdvancedOptions)
	{
		UISetEnabled(&OptionsMenu, OPTIONS_ID_LINK_MODE, 1);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_LINK_FLOW_CONTROL, 1);
	} else {
		UISetEnabled(&OptionsMenu, OPTIONS_ID_LINK_MODE, 0);
		UISetEnabled(&OptionsMenu, OPTIONS_ID_LINK_FLOW_CONTROL, 0);
	}

	//Draw additional legends
	DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_UD_DPAD, 20, 360, 4);
	FontPrintf(gsGlobal, 60, 360, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_SELECT_FIELD));
	DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_LR_DPAD, 20, 390, 4);
	FontPrintf(gsGlobal, 60, 390, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_TOGGLE_OPTION));

	DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_SELECT, 300, 360, 4);
	FontPrintf(gsGlobal, 340, 362, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_NETWORK_STATUS));

	//If the select button is pressed, bring up the network status screen.
	if(padstatus & PAD_SELECT)
		ShowNetworkStatus();

	return 0;
}

enum NETOPT_LINK_MODE {
	NETOPT_LINK_MODE_AUTO = 0,
	NETOPT_LINK_MODE_10M_HDX,
	NETOPT_LINK_MODE_10M_FDX,
	NETOPT_LINK_MODE_100M_HDX,
	NETOPT_LINK_MODE_100M_FDX,

	NETOPT_LINK_MODE_COUNT
};

//Return 1 if the top-level menu should refresh the list. 0 = normal exit. <0 = error
static int ShowOptions(void)
{
	static int LinkModeLabels[NETOPT_LINK_MODE_COUNT] = {
		SYS_UI_LBL_MODE_AUTO,
		SYS_UI_LBL_MODE_10MBIT_HDX,
		SYS_UI_LBL_MODE_10MBIT_FDX,
		SYS_UI_LBL_MODE_100MBIT_HDX,
		SYS_UI_LBL_MODE_100MBIT_FDX
	};
	int index, value, result;
	u8 OldSortTitles;

	OldSortTitles = RuntimeData.SortTitles;

	while(1)
	{
		//Options
		UISetValue(&OptionsMenu, OPTIONS_ID_SORT_TITLES, RuntimeData.SortTitles);

		//Network Options
		UISetValue(&OptionsMenu, OPTIONS_ID_NETWORK_DHCP, RuntimeData.UseDHCP);
		UISetValue(&OptionsMenu, OPTIONS_ID_IP_0, RuntimeData.ip_address[0]);
		UISetValue(&OptionsMenu, OPTIONS_ID_IP_1, RuntimeData.ip_address[1]);
		UISetValue(&OptionsMenu, OPTIONS_ID_IP_2, RuntimeData.ip_address[2]);
		UISetValue(&OptionsMenu, OPTIONS_ID_IP_3, RuntimeData.ip_address[3]);
		UISetValue(&OptionsMenu, OPTIONS_ID_NM_0, RuntimeData.subnet_mask[0]);
		UISetValue(&OptionsMenu, OPTIONS_ID_NM_1, RuntimeData.subnet_mask[1]);
		UISetValue(&OptionsMenu, OPTIONS_ID_NM_2, RuntimeData.subnet_mask[2]);
		UISetValue(&OptionsMenu, OPTIONS_ID_NM_3, RuntimeData.subnet_mask[3]);
		UISetValue(&OptionsMenu, OPTIONS_ID_GW_0, RuntimeData.gateway[0]);
		UISetValue(&OptionsMenu, OPTIONS_ID_GW_1, RuntimeData.gateway[1]);
		UISetValue(&OptionsMenu, OPTIONS_ID_GW_2, RuntimeData.gateway[2]);
		UISetValue(&OptionsMenu, OPTIONS_ID_GW_3, RuntimeData.gateway[3]);

		UISetValue(&OptionsMenu, OPTIONS_ID_ADVANCED_NETWORK, RuntimeData.AdvancedNetworkSettings);
		UISetEnum(&OptionsMenu, OPTIONS_ID_LINK_MODE, LinkModeLabels, NETOPT_LINK_MODE_COUNT);

		switch(RuntimeData.EthernetLinkMode)
		{
			case NETMAN_NETIF_ETH_LINK_MODE_10M_HDX:
				index = NETOPT_LINK_MODE_10M_HDX;
				break;
			case NETMAN_NETIF_ETH_LINK_MODE_10M_FDX:
				index = NETOPT_LINK_MODE_10M_FDX;
				break;
			case NETMAN_NETIF_ETH_LINK_MODE_100M_HDX:
				index = NETOPT_LINK_MODE_100M_HDX;
				break;
			case NETMAN_NETIF_ETH_LINK_MODE_100M_FDX:
				index = NETOPT_LINK_MODE_100M_FDX;
				break;
			default:
				index = NETOPT_LINK_MODE_AUTO;
		}

		UISetEnumSelectedIndex(&OptionsMenu, OPTIONS_ID_LINK_MODE, index);
		UISetValue(&OptionsMenu, OPTIONS_ID_LINK_FLOW_CONTROL, RuntimeData.EthernetFlowControl);

		if(UIExecMenu(&OptionsMenu, -1, NULL, &OptionsMenuUpdate) == OPTIONS_ID_OK)
		{	//User selected "OK"

			//Options
			RuntimeData.SortTitles = UIGetValue(&OptionsMenu, OPTIONS_ID_SORT_TITLES);

			//Network Options
			RuntimeData.UseDHCP = UIGetValue(&OptionsMenu, OPTIONS_ID_NETWORK_DHCP);
			RuntimeData.ip_address[0] = UIGetValue(&OptionsMenu, OPTIONS_ID_IP_0);
			RuntimeData.ip_address[1] = UIGetValue(&OptionsMenu, OPTIONS_ID_IP_1);
			RuntimeData.ip_address[2] = UIGetValue(&OptionsMenu, OPTIONS_ID_IP_2);
			RuntimeData.ip_address[3] = UIGetValue(&OptionsMenu, OPTIONS_ID_IP_3);
			RuntimeData.subnet_mask[0] = UIGetValue(&OptionsMenu, OPTIONS_ID_NM_0);
			RuntimeData.subnet_mask[1] = UIGetValue(&OptionsMenu, OPTIONS_ID_NM_1);
			RuntimeData.subnet_mask[2] = UIGetValue(&OptionsMenu, OPTIONS_ID_NM_2);
			RuntimeData.subnet_mask[3] = UIGetValue(&OptionsMenu, OPTIONS_ID_NM_3);
			RuntimeData.gateway[0] = UIGetValue(&OptionsMenu, OPTIONS_ID_GW_0);
			RuntimeData.gateway[1] = UIGetValue(&OptionsMenu, OPTIONS_ID_GW_1);
			RuntimeData.gateway[2] = UIGetValue(&OptionsMenu, OPTIONS_ID_GW_2);
			RuntimeData.gateway[3] = UIGetValue(&OptionsMenu, OPTIONS_ID_GW_3);
			RuntimeData.AdvancedNetworkSettings = UIGetValue(&OptionsMenu, OPTIONS_ID_ADVANCED_NETWORK);

			index = UIGetEnumSelectedIndex(&OptionsMenu, OPTIONS_ID_LINK_MODE);
			switch(index)
			{
				case NETOPT_LINK_MODE_10M_HDX:
					value = NETMAN_NETIF_ETH_LINK_MODE_10M_HDX;
					break;
				case NETOPT_LINK_MODE_10M_FDX:
					value = NETMAN_NETIF_ETH_LINK_MODE_10M_FDX;
					break;
				case NETOPT_LINK_MODE_100M_HDX:
					value = NETMAN_NETIF_ETH_LINK_MODE_100M_HDX;
					break;
				case NETOPT_LINK_MODE_100M_FDX:
					value = NETMAN_NETIF_ETH_LINK_MODE_100M_FDX;
					break;
				default:
					value = NETMAN_NETIF_ETH_LINK_MODE_AUTO;
					break;
			}
			RuntimeData.EthernetLinkMode = value;

			RuntimeData.EthernetFlowControl = UIGetValue(&OptionsMenu, OPTIONS_ID_LINK_FLOW_CONTROL);

			//Save settings.
			DisplayFlashStatusUpdate(SYS_UI_MSG_SAVING_HDD);
			if((result = SaveSettings()) != 0)
			{
				if(result == -ENOSPC)
					DisplayErrorMessage(SYS_UI_MSG_INSUFFICIENT_HDD_SPACE);
				else
					DisplayErrorMessage(SYS_UI_MSG_ERROR_SAVING_SETTINGS);
			}

#ifdef ENABLE_NETWORK_SUPPORT
			DisplayFlashStatusUpdate(SYS_UI_MSG_CONNECTING);

			//Apply settings.
			ethReinit();
			ethValidate();
#endif

			if(result == 0)
				break;
		}
		else	//User cancelled.
			break;
	}

	return(OldSortTitles != RuntimeData.SortTitles ? 1 : 0);
}

static void DrawMenuExitAnimation(void)
{
	int i;

	for(i=30; i>0; i--)
	{
		gsKit_prim_quad(gsGlobal, 0.0f, 0.0f,
				gsGlobal->Width, 0.0f,
				0.0f, gsGlobal->Height,
				gsGlobal->Width, gsGlobal->Height,
				0, GS_SETREG_RGBAQ(0, 0, 0, 0x80-(i*(0x80/30)), 0));
		SyncFlipFB();
	}
}

void MainMenu(void)
{
	int done, result;
	unsigned int PadStatus, PadStatus_raw, PadStatus_old_raw, PadStatusTemp, CurrentGameListGeneration, frame;
	unsigned short int PadRepeatDelayTicks, PadRepeatRateTicks;
	struct GameListDisplayData GameListDisplayData;
	struct HDLGameEntry *HDLGameList;
	unsigned int NumHDLGames;

	DEBUG_PRINTF("-= Main Menu =-\n");

#ifndef UI_TEST_MODE
	if(hddCheckPresent()!=0){
		DisplayErrorMessage(SYS_UI_MSG_NO_HDD);
		return;
	}

	if(hddCheckFormatted()!=0){
		if(DisplayPromptMessage(SYS_UI_MSG_HDD_FORMAT, SYS_UI_LBL_CANCEL, SYS_UI_LBL_OK)==2){
			hddFormat();
		}
		else{
			return;
		}
	}
#endif

	UpdateHardwareAddress();

	HDLGameList = NULL;
	NumHDLGames = LoadGameList(&HDLGameList, &GameListDisplayData);
	CurrentGameListGeneration = GetHDLGameListGeneration();
	done = 0;
	PadStatus_old_raw = 0;
	PadRepeatDelayTicks = UI_PAD_REPEAT_START_DELAY;
	PadRepeatRateTicks = UI_PAD_REPEAT_DELAY;
	frame = 0;

	while(!done){
		DrawBackground(gsGlobal, &BackgroundTexture);

		if(RuntimeData.IsRemoteClientConnected){
			EnterRemoteClientMenu(&RuntimeData);
		}
		PadStatus=ReadCombinedPadStatus();

		if(NumHDLGames>0){
			//For the pad repeat delay effect.
			PadStatus_raw=ReadCombinedPadStatus_raw();
			if(PadStatus_raw==0 || ((PadStatus_old_raw!=0) && (PadStatus_raw!=PadStatus_old_raw))){
				PadRepeatDelayTicks=UI_PAD_REPEAT_START_DELAY;
				PadRepeatRateTicks=UI_PAD_REPEAT_DELAY;

				PadStatusTemp=PadStatus_raw&~PadStatus_old_raw;
				PadStatus_old_raw=PadStatus_raw;
				PadStatus_raw=PadStatusTemp;
			}
			else{
				if(PadRepeatDelayTicks==0){
					//Allow the pad presses to repeat, but only after the pad repeat delay expires.
					if(PadRepeatRateTicks==0){
						PadRepeatRateTicks=UI_PAD_REPEAT_DELAY;
					}
					else{
						PadStatusTemp=PadStatus_raw&~PadStatus_old_raw;
						PadStatus_old_raw=PadStatus_raw;
						PadStatus_raw=PadStatusTemp;
					}

					PadRepeatRateTicks--;
				}
				else{
					PadStatusTemp=PadStatus_raw&~PadStatus_old_raw;
					PadStatus_old_raw=PadStatus_raw;
					PadStatus_raw=PadStatusTemp;

					PadRepeatDelayTicks--;
				}
			}

			if(PadStatus_raw&PAD_UP){
				/* If the user is viewing game titles in the middle of the game list, and the highlighted game is at the top of the viewable list, scroll the game list up by one record. */
				if(GameListDisplayData.SelectedGameIndex>0 && GameListDisplayData.SelectedGameIndex-GameListDisplayData.GameListViewPortStart==0){
					GameListDisplayData.GameListViewPortStart--;
				}

				if(GameListDisplayData.SelectedGameIndex>0) GameListDisplayData.SelectedGameIndex--;
			}
			if(PadStatus_raw&PAD_DOWN){
				if(GameListDisplayData.SelectedGameIndex<NumHDLGames-1) GameListDisplayData.SelectedGameIndex++;

				/* If the user is viewing game titles in the middle of the game list, and the highlighted game is at the bottom of the viewable list, scroll the game list down by one record. */
				if(GameListDisplayData.SelectedGameIndex-GameListDisplayData.GameListViewPortStart==GAME_LIST_MAX_DISPLAYED_GAMES){
					GameListDisplayData.GameListViewPortStart++;
				}
			}
		}

		/* User pressed CROSS to quit. */
		if(PadStatus&CancelButton){
			if(DisplayPromptMessage(SYS_UI_MSG_QUIT, SYS_UI_LBL_CANCEL, SYS_UI_LBL_OK)==2){
				done=1;
			}
		}
		else{
			if(PadStatus&PAD_START){
				WaitSema(RuntimeData.InstallationLockSema);
				if(StartInstallGame(&RuntimeData.ReadMode)==0){
					NumHDLGames=ReloadGameList(&HDLGameList, &GameListDisplayData);
				}
				SignalSema(RuntimeData.InstallationLockSema);
			}
			else if(PadStatus&PAD_SELECT){
				if(ShowOptions() == 1)
				{	//Refresh the list if there is a need to.
					NumHDLGames=ReloadGameList(&HDLGameList, &GameListDisplayData);
				}
			}

			/* These options are only valid if a game is selected. */
#ifndef UI_TEST_MODE
			if(NumHDLGames>0){
#endif
				if(PadStatus&SelectButton){
					if((result=UpdateGame(HDLGameList, GameListDisplayData.SelectedGameIndex))<0){
						DisplayErrorMessage(SYS_UI_MSG_GAME_UPDATE_FAIL);
					}
					else if(result==0){
						NumHDLGames=ReloadGameList(&HDLGameList, &GameListDisplayData);
					}
				}

				if(PadStatus&PAD_TRIANGLE){
					if(DisplayPromptMessage(SYS_UI_MSG_PROMPT_DELETE_GAME, SYS_UI_LBL_CANCEL, SYS_UI_LBL_OK)==2){
						DeleteGame(HDLGameList, GameListDisplayData.SelectedGameIndex);
						NumHDLGames=ReloadGameList(&HDLGameList, &GameListDisplayData);
					}
				}
#ifndef UI_TEST_MODE
			}
#endif
		}

		if (frame % UPDATE_INTERVAL == 0)
		{
#ifdef ENABLE_NETWORK_SUPPORT
			ethGetIPAddressDisplay(IPAddressDisplay);
#endif
		}

		//Prevent updates while the main menu is being drawn. Lock it here, so that RedrawMainMenu() will not carry an outdated pointer to the game list with it.
		LockCentralHDLGameList();

		//If the list was updated (by the network client), get the updated game list.
		//Get a new reference to the game list here, to ensure that it is fresh between the locking and unlocking.
		if(GetHDLGameListGeneration()!=CurrentGameListGeneration){
			CurrentGameListGeneration=GetHDLGameListGeneration();
			NumHDLGames=GetGameListData(&HDLGameList, &GameListDisplayData);
		}

		RedrawMainMenu(HDLGameList, NumHDLGames, GameListDisplayData.SelectedGameIndex, GameListDisplayData.GameListViewPortStart);
		UnlockCentralHDLGameList();
		SyncFlipFB();

		frame++;
	}

	DrawMenuExitAnimation();
}

static int StartInstallGame(sceCdRMode *ReadMode){
	mcIcon McSaveIconSys;
	char DiscID[11], StartupFname[32], PartName[38], SaveFolderPath[38];
	char *ExternalIconSourcePath;
	unsigned char DiscType, TRType, TRMode, SaveLoaded;
	struct GameSettings GameSettings;
	unsigned int SectorsInDiscLayer0, SectorsInDiscLayer1;
	int result, IconSource;

	if((result=ShowWaitForDiscDialog())!=0){
		DEBUG_PRINTF("User aborted.\n");
		return result;
	}

	if((result=InitGameCDVDInformation(ReadMode, DiscID, StartupFname, &DiscType, &SectorsInDiscLayer0, &SectorsInDiscLayer1))>=0){
		DisplayFlashStatusUpdate(SYS_UI_MSG_PLEASE_WAIT);

		strcpy(PartName, "hdd0:");	/* Before anything else, just copy "hdd0:" just before the partition name. */
		if(CheckForExistingGameInstallation(DiscID, &PartName[5], sizeof(PartName)-5)){
			if(DisplayPromptMessage(SYS_UI_MSG_INST_OVERWRITE, SYS_UI_LBL_OK, SYS_UI_LBL_CANCEL)!=1){
				DEBUG_PRINTF("Error: Game was already installed, but the user did not want to overwrite it.\n");
				result=-EEXIST;
			}
		}

		if(result>=0){
			memset(GameSettings.FullTitle, 0, sizeof(GameSettings.FullTitle));
			memset(GameSettings.OSDTitleLine1, 0, sizeof(GameSettings.OSDTitleLine1));
			memset(GameSettings.OSDTitleLine2, 0, sizeof(GameSettings.OSDTitleLine2));
			if((SaveLoaded=(LoadMcSaveSys(SaveFolderPath, &McSaveIconSys, DiscID)>=0)?1:0)){
				DEBUG_PRINTF("Memory card save at %s loaded. Title: %s\n", SaveFolderPath, (const char*)McSaveIconSys.title);

				ConvertMcTitle(&McSaveIconSys, GameSettings.OSDTitleLine1, GameSettings.OSDTitleLine2);
				swprintf(GameSettings.FullTitle, sizeof(GameSettings.FullTitle)/sizeof(wchar_t), L"%s%s", GameSettings.OSDTitleLine1, GameSettings.OSDTitleLine2);
			}

			GameSettings.CompatibilityModeFlags=GameSettings.UseMDMA0=0;

RedisplayGameOptionScreen:
			/* Give the user some time to adjust the game's title and settings. */
			if((result=GetUserGameSettings(&GameSettings))==0){
RedisplayGameIconOptionScreen:
				if((IconSource=GetUserIconSourceChoice(&ExternalIconSourcePath))<0){
					goto RedisplayGameOptionScreen;
				}

				if(IconSource!=ICON_SOURCE_DEFAULT){
					if(LoadMcSaveSysFromPath(IconSource==ICON_SOURCE_SAVE?SaveFolderPath:ExternalIconSourcePath, &McSaveIconSys)<0){
						if(DisplayPromptMessage(SYS_UI_MSG_ICON_LOAD_FAIL, SYS_UI_LBL_NO, SYS_UI_LBL_YES)==2){
							IconSource=ICON_SOURCE_DEFAULT;
						}
						else{
							if(ExternalIconSourcePath!=NULL) free(ExternalIconSourcePath);
							goto RedisplayGameIconOptionScreen;
						}
					}
				}

				if(GameSettings.UseMDMA0){
					/* Use MDMA Mode 0 */
					TRType=ATA_XFER_MODE_MDMA;
					TRMode=0;
				}
				else{
					/* Use UDMA Mode 4 */
					TRType=ATA_XFER_MODE_UDMA;
					TRMode=4;
				}

				/* Generate the partition name for mounting. */
				GeneratePartName(PartName, DiscID, GameSettings.OSDTitleLine1);

				/* Install the game. */
				if(CheckForExistingGameInstallation(DiscID, &PartName[5], sizeof(PartName)-5)){
					DEBUG_PRINTF("Old game installation detected. Overwriting...\n");
					RemoveGameInstallation(PartName);
				}

				if((result=InstallGameFromCDVDDrive(ReadMode, PartName, GameSettings.FullTitle, DiscID, StartupFname, DiscType, SectorsInDiscLayer0, SectorsInDiscLayer1, GameSettings.CompatibilityModeFlags, TRType, TRMode))==0){
InstallOSDResources_start:
					/* Install OSD resources. */
					if((result=InstallGameInstallationOSDResources(&PartName[5], &GameSettings, IconSource!=ICON_SOURCE_DEFAULT?&McSaveIconSys:NULL, IconSource!=ICON_SOURCE_DEFAULT?(IconSource==ICON_SOURCE_SAVE?SaveFolderPath:ExternalIconSourcePath):NULL))>=0){
						if(IconSource==ICON_SOURCE_SAVE || IconSource==ICON_SOURCE_EXTERNAL){
							if(result!=ICON_SOURCE_SAVE){	//If a savedata icon was not loaded (includes when using the "external" option).
								if(DisplayPromptMessage(SYS_UI_MSG_ICON_LOAD_FAIL, SYS_UI_LBL_NO, SYS_UI_LBL_YES)==2){
									IconSource=ICON_SOURCE_DEFAULT;
									goto InstallOSDResources_start;
								}
								else{
									goto RedisplayGameIconOptionScreen;
								}
							}
						}
					}
					else{
						DEBUG_PRINTF("Error installing OSD files: %d\n", result);
						DisplayErrorMessage(SYS_UI_MSG_OSD_INST_FAILED);
						RemoveGameInstallation(PartName);
					}

					if(result>=0){
						DisplayInfoMessage(SYS_UI_MSG_INST_COMPLETE);
						result=0;
					}
				}

				if(ExternalIconSourcePath!=NULL) free(ExternalIconSourcePath);
				if(result>=0) result=0;
			}
			else DEBUG_PRINTF("User canceled game installation.\n");
		}
	}
	else{
		sceCdStop();
		sceCdSync(0);

		DEBUG_PRINTF("Error installing game: %d\n", result);
	}

	return result;
}

#define NUM_KEYS_PER_ROW	12
#define NUM_KEY_COLUMNS		5

enum KeyboardKeySets{
	KEYBOARD_KEYSET_UPPER_ALPHA=0,
	KEYBOARD_KEYSET_LOWER_ALPHA,
	NUM_KEYBOARD_KEYSETS,
};

static const char *KeyboardKeys[NUM_KEYBOARD_KEYSETS][NUM_KEY_COLUMNS][NUM_KEYS_PER_ROW]={
	{	/* Upper alphabet keyset */
		{"!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "_", "+"},
		{"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "{", "}"},
		{"A", "S", "D", "F", "G", "H", "J", "K", "L", ":", "\"", "\n"},
		{"Z", "X", "C", "V", "B", "N", "M", "<", ">", "?", "~"}
	},
	{	/* Lower alphabet keyset */
		{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "="},
		{"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "[", "]"},
		{"a", "s", "d", "f", "g", "h", "j", "k", "l", ";", "\"", "\n"},
		{"z", "x", "c", "v", "b", "n", "m", ",", ".", "/", "`"}
	},
};

static inline int IsWhitespaceCharacter(const char *string){
	return(string[0]==' ' && string[1]=='\0');
}

static inline int IsNewlineCharacter(const char *string){
	return(string[0]=='\n' && string[1]=='\0');
}

int DisplaySoftKeyboard(wchar_t *buffer, unsigned int length, unsigned int options)
{	//Length of buffer is in wchar_t units, excluding the NULL terminator.
	unsigned int PadStatus, PadStatus_raw, PadStatus_old_raw, PadStatusTemp;
	unsigned short int PadRepeatDelayTicks, PadRepeatRateTicks;

	unsigned char done, SelectedKeyboardKeyset, SelectedKeyX, SelectedKeyY, x, y, UpdateCursor;
	wchar_t *CharacterBuffer;
	int result, NumCharsInString, BufferLength, CursorPosition, i, DisplayedCharStartPos, CursorPositionOnScreen, DisplayedCharCount;
	const char *NewCharacter;
	short int x_rel, y_rel;

	BufferLength=(length+1)*sizeof(wchar_t);
	CharacterBuffer=malloc(BufferLength);
	wcscpy(CharacterBuffer, buffer);

	PadStatus_old_raw=0;
	PadRepeatDelayTicks=UI_PAD_REPEAT_START_DELAY;
	PadRepeatRateTicks=UI_PAD_REPEAT_DELAY;

	done = 0;
	result= 0;
	SelectedKeyX = 0;
	SelectedKeyY = 0;
	SelectedKeyboardKeyset = KEYBOARD_KEYSET_UPPER_ALPHA;
	NumCharsInString = wcslen(buffer);
	DisplayedCharStartPos = 0;
	CursorPosition = 0;
	CursorPositionOnScreen = CursorPosition;
	DisplayedCharCount = 0;

	//Wait for all buttons to be released.
	while(ReadCombinedPadStatus_raw() != 0){};

	while(!done)
	{
		DrawBackground(gsGlobal, &BackgroundTexture);

		PadStatus=ReadCombinedPadStatus();

		//For the pad repeat delay effect.
		PadStatus_raw=ReadCombinedPadStatus_raw();
		if(PadStatus_raw==0 || ((PadStatus_old_raw!=0) && (PadStatus_raw!=PadStatus_old_raw))){
			PadRepeatDelayTicks=UI_PAD_REPEAT_START_DELAY;
			PadRepeatRateTicks=UI_PAD_REPEAT_DELAY;

			PadStatusTemp=PadStatus_raw&~PadStatus_old_raw;
			PadStatus_old_raw=PadStatus_raw;
			PadStatus_raw=PadStatusTemp;
		}
		else{
			if(PadRepeatDelayTicks==0){
				//Allow the pad presses to repeat, but only after the pad repeat delay expires.
				if(PadRepeatRateTicks==0){
					PadRepeatRateTicks=UI_PAD_REPEAT_DELAY;
				}
				else{
					PadStatusTemp=PadStatus_raw&~PadStatus_old_raw;
					PadStatus_old_raw=PadStatus_raw;
					PadStatus_raw=PadStatusTemp;
				}

				PadRepeatRateTicks--;
			}
			else{
				PadStatusTemp=PadStatus_raw&~PadStatus_old_raw;
				PadStatus_old_raw=PadStatus_raw;
				PadStatus_raw=PadStatusTemp;

				PadRepeatDelayTicks--;
			}
		}

		UpdateCursor = CursorPositionOnScreen > DisplayedCharCount;

		/* Draw the text and the cursor. */
		gsKit_prim_quad(gsGlobal, SOFT_KEYBOARD_DISPLAY_X_LOCATION, SOFT_KEYBOARD_DISPLAY_Y_LOCATION, 620, SOFT_KEYBOARD_DISPLAY_Y_LOCATION, SOFT_KEYBOARD_DISPLAY_X_LOCATION, SOFT_KEYBOARD_DISPLAY_Y_LOCATION+SOFT_KEYBOARD_CURSOR_HEIGHT, 620, SOFT_KEYBOARD_DISPLAY_Y_LOCATION+SOFT_KEYBOARD_CURSOR_HEIGHT, 4, GS_GREY);
		DisplayedCharCount = wFontPrintField(gsGlobal, SOFT_KEYBOARD_DISPLAY_X_LOCATION, SOFT_KEYBOARD_DISPLAY_Y_LOCATION+8, 1, SOFT_KEYBOARD_FONT_SCALE, GS_WHITE_FONT, &CharacterBuffer[DisplayedCharStartPos], SOFT_KEYBOARD_MAX_DISPLAYED_WIDTH, CursorPositionOnScreen);

		//Changing the characters may result in the displayable character count changing.
		if(UpdateCursor)
		{	/*	Recompute the cursor's position. If it changes, then it will be corrected in the next frame.
				Ideally, FreeType could be used to simulate how much space is required, but it is computationally expensive to make multiple runs. */
			CursorPositionOnScreen = DisplayedCharCount;

			//Update the start position
			DisplayedCharStartPos = CursorPosition >= DisplayedCharCount ? CursorPosition - DisplayedCharCount : 0;
		}

		if(PadStatus&CancelButton)
		{
			if(DisplayPromptMessage(SYS_UI_MSG_CANCEL_INPUT, SYS_UI_LBL_CANCEL, SYS_UI_LBL_OK)==2)
			{
				result=done=1;
				continue;
			}
		}
		else if(PadStatus&PAD_START)
		{	/* Highlight the "ENTER" key. */
			SelectedKeyX=11;
			SelectedKeyY=2;
		}
		else if(PadStatus&PAD_UP && KeyboardKeys[SelectedKeyboardKeyset][SelectedKeyY-1][SelectedKeyX]!='\0')
		{
			if(SelectedKeyY>0) SelectedKeyY--;
		}
		else if(PadStatus&PAD_DOWN)
		{
			if(SelectedKeyY<NUM_KEY_COLUMNS-1 && KeyboardKeys[SelectedKeyboardKeyset][SelectedKeyY+1][SelectedKeyX]!='\0')
				SelectedKeyY++;
		}
		else if(PadStatus&PAD_LEFT)
		{
			if(KeyboardKeys[SelectedKeyboardKeyset][SelectedKeyY][SelectedKeyX-1]!='\0' && SelectedKeyX>0)
				SelectedKeyX--;
		}
		else if(PadStatus&PAD_RIGHT)
		{
			if(KeyboardKeys[SelectedKeyboardKeyset][SelectedKeyY][SelectedKeyX+1]!='\0' && SelectedKeyX<NUM_KEYS_PER_ROW-1)
				SelectedKeyX++;
		}
		else if(PadStatus&PAD_SELECT)
		{
			SelectedKeyboardKeyset++;
			if(SelectedKeyboardKeyset>=NUM_KEYBOARD_KEYSETS) SelectedKeyboardKeyset=0;
		}
		else if(PadStatus_raw != 0)
		{
			if(PadStatus_raw & SelectButton)
			{
				if(SelectedKeyX==11&&SelectedKeyY==2)
				{	//ENTER button
					result=0;
					done=1;
					wcscpy(buffer, CharacterBuffer);
					continue;
				}
				else
				{
					NewCharacter=KeyboardKeys[SelectedKeyboardKeyset][SelectedKeyY][SelectedKeyX];

					/* Do bounds checking! */
					if(NumCharsInString < length)
					{
						/* First move everything after the cursor forward by one space. */
						CharacterBuffer[NumCharsInString+1]='\0';
						for(i=NumCharsInString; i>CursorPosition; i--)
							CharacterBuffer[i]=CharacterBuffer[i-1];

						/* Next, insert the selected character at the cursor's position.
						   Update the number of characters and the position of the cursor. */
						CharacterBuffer[CursorPosition]=NewCharacter[0];

						NumCharsInString++;
						CursorPosition++;
						CursorPositionOnScreen++;
					}
				}
			}
			else if(PadStatus_raw & PAD_L1)
			{
				if(CursorPosition>0)
				{
					CursorPosition--;

					if(CursorPositionOnScreen>0) CursorPositionOnScreen--;
					if(CursorPositionOnScreen==0)
						DisplayedCharStartPos=CursorPosition;
				}
			}
			else if(PadStatus_raw & PAD_R1)
			{
				if(CursorPosition < NumCharsInString)
				{
					CursorPosition++;
					CursorPositionOnScreen++;	//Will be updated above.
				}
			}
			else if(PadStatus_raw & PAD_TRIANGLE)
			{
				if(NumCharsInString<length)
				{
					/* First move everything after the cursor forward by one space. */
					CharacterBuffer[NumCharsInString+1]='\0';
					for(i=NumCharsInString; i>CursorPosition; i--)
						CharacterBuffer[i]=CharacterBuffer[i-1];

					/* Next, insert the selected character at the cursor's position (And then finally, update the number of characters and the position of the cursor). */
					CharacterBuffer[CursorPosition]=' ';

					NumCharsInString++;
					CursorPosition++;
					CursorPositionOnScreen++;	//Will be updated above.
				}
			}
			else if(PadStatus_raw & PAD_SQUARE)
			{
				if(CursorPosition>0)
				{	/* First move everything after the cursor back by one space. */
					for(i=CursorPosition-1; i<NumCharsInString-1; i++)
						CharacterBuffer[i]=CharacterBuffer[i+1];

					/* Next, replace the (previously) last character with a NULL terminator (And then finally, update the number of characters and the position of the cursor). */
					NumCharsInString--;
					CharacterBuffer[NumCharsInString]='\0';
					CursorPosition--;

					if(CursorPosition > DisplayedCharCount)
					{	//Scroll the start position instead.
						if(CursorPositionOnScreen==0)
						{
							DisplayedCharStartPos=CursorPosition;
						} else {
							--DisplayedCharStartPos;
						}
					}
					else
					{
						if(CursorPositionOnScreen>0) CursorPositionOnScreen--;
						if(CursorPositionOnScreen==0)
						{	//Scroll the start position instead.
							DisplayedCharStartPos=CursorPosition;
						}
					}
				}
			}
		}

		/* Draw the soft keyboard. */
		for(y=0; y<NUM_KEY_COLUMNS; y++)
		{
			for(x=0; x<NUM_KEYS_PER_ROW; x++)
			{
				if(KeyboardKeys[SelectedKeyboardKeyset][y][x]!=NULL)
				{
					/* The ENTER/RETURN key (11, 2) needs to be longer. */
					if(IsNewlineCharacter(KeyboardKeys[SelectedKeyboardKeyset][y][x]) || IsWhitespaceCharacter(KeyboardKeys[SelectedKeyboardKeyset][y][x]))
					{
						gsKit_prim_quad(gsGlobal,	SOFT_KEYBOARD_X_LOCATION+x*36-8, SOFT_KEYBOARD_Y_LOCATION+y*36-8,
										SOFT_KEYBOARD_X_LOCATION+x*36+96, SOFT_KEYBOARD_Y_LOCATION+y*36-8,
										SOFT_KEYBOARD_X_LOCATION+x*36-8, SOFT_KEYBOARD_Y_LOCATION+y*36+24,
										SOFT_KEYBOARD_X_LOCATION+x*36+96, SOFT_KEYBOARD_Y_LOCATION+y*36+24,
										4,
										(y==SelectedKeyY&&x==SelectedKeyX)?GS_LGREY:GS_GREY);
					}
					else
					{
						gsKit_prim_quad(gsGlobal,	SOFT_KEYBOARD_X_LOCATION+x*36-8, SOFT_KEYBOARD_Y_LOCATION+y*36-8,
										SOFT_KEYBOARD_X_LOCATION+x*36+24, SOFT_KEYBOARD_Y_LOCATION+y*36-8,
										SOFT_KEYBOARD_X_LOCATION+x*36-8, SOFT_KEYBOARD_Y_LOCATION+y*36+24,
										SOFT_KEYBOARD_X_LOCATION+x*36+24, SOFT_KEYBOARD_Y_LOCATION+y*36+24,
										4,
										(y==SelectedKeyY&&x==SelectedKeyX)?GS_LGREY:GS_GREY);
					}

					FontPrintf(gsGlobal,	SOFT_KEYBOARD_X_LOCATION+x*36, SOFT_KEYBOARD_Y_LOCATION+y*36-8,
								1,
								1.2f,
								GS_WHITE_FONT,
								IsNewlineCharacter(KeyboardKeys[SelectedKeyboardKeyset][y][x])?"ENTER":(IsWhitespaceCharacter(KeyboardKeys[SelectedKeyboardKeyset][y][x])?"SPACE":KeyboardKeys[SelectedKeyboardKeyset][y][x]));
				}
			}
		}

		/* Draw the button legend. */
		DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_DPAD, 10, 360, 4);
		FontPrintf(gsGlobal, 90, 362, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_SELECT_KEY));

		DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_L1, 10, 390, 4);
		DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_R1, 30, 390, 4);
		FontPrintf(gsGlobal, 90, 392, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_MOVE_CURSOR));

		DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_SELECT, 320, 360, 4);
		FontPrintf(gsGlobal, 360, 362, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_TOGGLE_CHAR_SET));

		DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_START, 320, 390, 4);
		FontPrintf(gsGlobal, 360, 392, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_ENTER));

		DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_SQUARE, 10, 420, 4);
		FontPrintf(gsGlobal, 90, 422, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_DELETE_CHAR));

		DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_TRIANGLE, 320, 420, 4);
		FontPrintf(gsGlobal, 360, 422, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_INSERT_SPACE));

		DrawButtonLegend(gsGlobal, &PadLayoutTexture, SelectButton == PAD_CIRCLE ? BUTTON_TYPE_CIRCLE : BUTTON_TYPE_CROSS, 10, 450, 4);
		FontPrintf(gsGlobal, 90, 452, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_OK));

		DrawButtonLegend(gsGlobal, &PadLayoutTexture, CancelButton == PAD_CIRCLE ? BUTTON_TYPE_CIRCLE : BUTTON_TYPE_CROSS, 320, 450, 4);
		FontPrintf(gsGlobal, 360, 452, 1, 1.0f, GS_WHITE_FONT, GetUILabel(SYS_UI_LBL_CANCEL));

		SyncFlipFB();
	}

	free(CharacterBuffer);

	return result;
}

int ShowWaitForDiscDialog(void){
	unsigned char DiscType;
	int result;
	u32 PadStatus;

	DisplayFlashStatusUpdate(SYS_UI_MSG_READING_DISC);
	result=0;

	while(1){
		DiscType=sceCdGetDiskType();
		PadStatus=ReadCombinedPadStatus();

		if(DiscType==SCECdNODISC){
			DisplayErrorMessage(SYS_UI_MSG_NO_DISC);
			result=1;
			break;
		}
		else if(DiscType==SCECdDETCT || DiscType==SCECdDETCTCD || DiscType==SCECdDETCTDVDS || DiscType==SCECdDETCTDVDD){
			DisplayFlashStatusUpdate(SYS_UI_MSG_READING_DISC);
		}
		else if(DiscType>=SCECdPS2CD && DiscType<=SCECdPS2DVD){
			/* Supported disc types */
			result=0;
			break;
		}
		else{	/* Unsupported disc types */
			DisplayErrorMessage(SYS_UI_MSG_UNSUP_DISC);
			result=1;
			break;
		}
	}

	return result;
}
