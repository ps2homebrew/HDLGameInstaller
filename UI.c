#include <kernel.h>
#include <libpad.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <osd_config.h>
#include <limits.h>
#include <wchar.h>

#include <gsKit.h>

#include "main.h"
#ifdef ENABLE_NETWORK_SUPPORT
#include <ps2ip.h>
#include <netman.h>
#endif

#include "HDLGameList.h"
#include "system.h"
#include "pad.h"
#include "graphics.h"
#include "font.h"
#include "UI.h"
#include "menu.h"

extern int errno __attribute__((section("data")));

GSGLOBAL *gsGlobal;
GSTEXTURE BackgroundTexture;
GSTEXTURE PadLayoutTexture;
GSTEXTURE DeviceIconTexture;

static void *gFontBuffer = NULL;
static int gFontBufferSize;
unsigned short int SelectButton, CancelButton;

#define NUM_SUPPORTED_LANGUAGES	8

static int language=LANGUAGE_ENGLISH;

#include "lang.c"

static char *LangStringTable[SYS_UI_MSG_COUNT];
static char *LangLblStringTable[SYS_UI_LBL_COUNT];
static u8 LangStringWrapTable[(SYS_UI_MSG_COUNT + 7) / 8];

static void UnloadLanguage(void);

static void WaitForDevice(void)
{
	nopdelay();
	nopdelay();
	nopdelay();
	nopdelay();
	nopdelay();
	nopdelay();
	nopdelay();
	nopdelay();
}

static int FormatLanguageString(const char *in, int len, char *out)
{
	wchar_t wchar1, wchar2;
	int ActualLength, CharLen1, CharLen2;

	ActualLength = 0;
	CharLen1 = mbtowc(&wchar1, in, len);
	while(CharLen1 > 0 && wchar1 != '\0')
	{
		CharLen2 = mbtowc(&wchar2, in + CharLen1, len - CharLen1);
		if((CharLen2 > 0) && (wchar1 == '\\' && wchar2 != '\0'))
		{
			switch(wchar2)
			{	//When a translation file is used, escape characters appear like "\n" in the file.
				case 'n':
					*out = '\n';
					out++;
					ActualLength++;
					break;
			}

			in += (CharLen1 + CharLen2);
			len -= (CharLen1 + CharLen2);
			CharLen2 = mbtowc(&wchar2, in, len);
		} else {
			memcpy(out, in, CharLen1);
			out += CharLen1;
			ActualLength += CharLen1;
			in += CharLen1;
			len -= CharLen1;
		}

		CharLen1 = CharLen2;
		wchar1 = wchar2;
	}
	*out = '\0';

	return(ActualLength + 1);
}

static void BreakLongLanguageString(char *str)
{
	wchar_t wchar;
	int CharLen, ScreenLineLenPx, LineMaxPx, PxSinceLastSpace, len, width;
	char *LastWhitespaceOut;

	LineMaxPx = gsGlobal->Width - 2*UI_OFFSET_X;

	// Line lengths are counted from the last newline character (either discovered or inserted).
	ScreenLineLenPx = 0;
	PxSinceLastSpace = 0;
	len = strlen(str) + 1;
	CharLen = mbtowc(&wchar, str, len);
	LastWhitespaceOut = NULL;
	while(CharLen > 0 && wchar != '\0')
	{
		switch(wchar)
		{
			case '\n':
				ScreenLineLenPx = 0;
				LastWhitespaceOut = NULL;
				PxSinceLastSpace = 0;
				break;
			case ' ':	//Record where the latest whitespace is.
				LastWhitespaceOut = str;
				PxSinceLastSpace = 0;
				break;
		}

		if (wchar != '\n')
		{
			width = FontGetGlyphWidth(gsGlobal, wchar);

			// To wrap long lines, replace the last whitespace with a newline character
			if(ScreenLineLenPx + width >= LineMaxPx)
			{
				if(LastWhitespaceOut != NULL)
				{
					ScreenLineLenPx = PxSinceLastSpace;
					*LastWhitespaceOut = '\n';
					LastWhitespaceOut = NULL;
				}
			}

			ScreenLineLenPx += width;
			if(wchar != ' ')
				PxSinceLastSpace += width;
		}

		str += CharLen;
		len -= CharLen;

		CharLen = mbtowc(&wchar, str, len);
	}
}

static int ParseLanguageFile(char **array, FILE *file, unsigned int ExpectedNumLines)
{
	int result, LinesLoaded, len;
	unsigned char BOMTemp[3];
	char line[512];

	if(fread(BOMTemp, 1, 3, file)!=3 || (BOMTemp[0]!=0xEF || BOMTemp[1]!=0xBB || BOMTemp[2]!=0xBF))
	{	//Check for the BOM byte sequence. Skip it, if it exists.
		rewind(file);
	}

	result=0;
	for(LinesLoaded = 0; fgets(line, sizeof(line), file) != NULL; LinesLoaded++)
	{
		len = strlen(line);

		if(len >= 1 && line[len - 1] == '\n')	//Remove the newline character, if it exists.
		{
			line[len - 1] = '\0';
			len--;
		}

		if((array[LinesLoaded] = malloc(len + 1)) != NULL)
		{
			len = FormatLanguageString(line, len + 1, array[LinesLoaded]);
			array[LinesLoaded] = realloc(array[LinesLoaded], len);
		} else {
			result=-ENOMEM;
			break;
		}
	}

	if(result == 0)
	{
		if(LinesLoaded != ExpectedNumLines)
		{
			printf("ParseLanguageFile: Mismatched number of lines (%u/%d)\n", LinesLoaded, ExpectedNumLines);
			result = -1;
		}
	}

	return result;
}

static int ParseFontListFile(char **array, FILE *file, unsigned int ExpectedNumLines)
{
	int result, LinesLoaded, len;
	char line[256];

	result=0;
	for(LinesLoaded = 0; fgets(line, sizeof(line), file) != NULL; LinesLoaded++)
	{
		len = strlen(line);

		if(len >= 1 && line[len - 1] == '\n')	//Remove the newline character, if it exists.
		{
			line[len - 1] = '\0';
			len--;
		}

		if(LinesLoaded < ExpectedNumLines)
		{
			if((array[LinesLoaded] = malloc(len + 1)) != NULL)
			{
				len = FormatLanguageString(line, len + 1, array[LinesLoaded]);
				array[LinesLoaded] = realloc(array[LinesLoaded], len);
			} else {
				result = -ENOMEM;
				break;
			}
		}
	}

	if(result == 0)
	{
		if(LinesLoaded!=ExpectedNumLines)
		{
			printf("ParseFontListFile: Mismatched number of lines (%u/%d)\n", LinesLoaded, ExpectedNumLines);
			result = -1;
		}
	}

	return result;
}

static const char DefaultFontFilename[]="NotoSans-Bold.ttf";

static char *GetDefaultFontFilePath(void)
{
	char *result;

	if((result=malloc(sizeof(DefaultFontFilename)+6+2))!=NULL)
		sprintf(result, "lang/%s", DefaultFontFilename);

	return result;
}

static char *GetFontFilePath(unsigned int language)
{
	char *FontFileArray[NUM_SUPPORTED_LANGUAGES], *result, *pFontFilename;
	FILE *file;
	int i;

	result=NULL;
	memset(FontFileArray, 0, sizeof(FontFileArray));

	while((file = fopen("lang/fonts.txt", "r")) == NULL)
	{
		if(errno != ENODEV)
			break;

		WaitForDevice();
	}

	if(file != NULL)
	{
		if(ParseFontListFile(FontFileArray, file, NUM_SUPPORTED_LANGUAGES)==0)
		{
			pFontFilename=FontFileArray[language];

			if((result = malloc(strlen(pFontFilename)+6)) != NULL)
				sprintf(result, "lang/%s", pFontFilename);
		} else
			result=GetDefaultFontFilePath();

		for(i=0; i<NUM_SUPPORTED_LANGUAGES; i++)
			if(FontFileArray[i]!=NULL) free(FontFileArray[i]);

		fclose(file);
	} else
		result=GetDefaultFontFilePath();

	return result;
}

static int LoadLanguageStrings(unsigned int language)
{
	int result;
	FILE *file;
	char path[20];
	static const char *LanguageShortForms[NUM_SUPPORTED_LANGUAGES]={
		"JA",
		"EN",
		"FR",
		"SP",
		"GE",
		"IT",
		"DU",
		"PO"
	};

	memset(LangStringTable, 0, sizeof(LangStringTable));
	memset(LangLblStringTable, 0, sizeof(LangLblStringTable));

	sprintf(path, "lang/strings_%s.txt", LanguageShortForms[language]);
	if((file = fopen(path, "r")) != NULL)
	{
		result=ParseLanguageFile(LangStringTable, file, SYS_UI_MSG_COUNT);

		fclose(file);
		if(result==0)
		{
			sprintf(path, "lang/labels_%s.txt", LanguageShortForms[language]);
			if((file = fopen(path, "r")) != NULL)
			{
				result=ParseLanguageFile(LangLblStringTable, file, SYS_UI_LBL_COUNT);
				fclose(file);
			}
			else result = -errno;
		}
	}
	else result = -errno;

	if(result!=0)
		UnloadLanguage();

	return result;
}

static int LoadDefaultLanguageStrings(void)
{
	int result, LinesLoaded, len;

	result = 0;
	memset(LangStringTable, 0, sizeof(LangStringTable));
	memset(LangLblStringTable, 0, sizeof(LangLblStringTable));

	//Load default strings
	for(LinesLoaded = 0; LinesLoaded < SYS_UI_MSG_COUNT; LinesLoaded++)
	{
		len = strlen(DefaultLanguageStringTable[LinesLoaded]);

		if((LangStringTable[LinesLoaded] = malloc(len + 1)) != NULL)
		{
			len = FormatLanguageString(DefaultLanguageStringTable[LinesLoaded], len + 1, LangStringTable[LinesLoaded]);
			LangStringTable[LinesLoaded] = realloc(LangStringTable[LinesLoaded], len);
		} else {
			result=-ENOMEM;
			break;
		}
	}

	if(result == 0)
	{
		//Load default labels
		for(LinesLoaded = 0; LinesLoaded < SYS_UI_LBL_COUNT; LinesLoaded++)
		{
			len = strlen(DefaultLanguageLabelStringTable[LinesLoaded]);

			if((LangLblStringTable[LinesLoaded] = malloc(len + 1)) != NULL)
			{
				len = FormatLanguageString(DefaultLanguageLabelStringTable[LinesLoaded], len + 1, LangLblStringTable[LinesLoaded]);
				LangLblStringTable[LinesLoaded] = realloc(LangLblStringTable[LinesLoaded], len);
			} else {
				result=-ENOMEM;
				break;
			}
		}
	}

	if(result!=0)
		UnloadLanguage();

	return result;
}

static void UnloadLanguage(void)
{
	unsigned int i;

	for(i=0; i<SYS_UI_MSG_COUNT; i++)
	{
		if(LangStringTable[i]!=NULL)
		{
			free(LangStringTable[i]);
			LangStringTable[i]=NULL;
		}
	}

	for(i=0; i<SYS_UI_LBL_COUNT; i++)
	{
		if(LangLblStringTable[i]!=NULL)
		{
			free(LangLblStringTable[i]);
			LangLblStringTable[i]=NULL;
		}
	}
}

const char *GetUIString(unsigned int id)
{
	if(!(LangStringWrapTable[id / 8] & (1 << (id % 8))))
	{
		BreakLongLanguageString(LangStringTable[id]);
		LangStringWrapTable[id / 8] |= (1 << (id % 8));
	}

	return(LangStringTable[id]);
}

const char *GetUILabel(unsigned int id)
{
	return(LangLblStringTable[id]);
}

static void InitGraphics(void)
{
	/* Initilize DMAKit */
	dmaKit_init(D_CTRL_RELE_OFF,D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);

	/* Initialize the DMAC */
	dmaKit_chan_init(DMA_CHANNEL_GIF);

	/* Initilize the GS */
	gsGlobal=gsKit_init_global();

	gsGlobal->DoubleBuffering = GS_SETTING_OFF;	/* Disable double buffering to get rid of the "Out of VRAM" error */
	gsGlobal->PrimAlphaEnable = GS_SETTING_ON;	/* Enable alpha blending for primitives. */
	gsGlobal->ZBuffering = GS_SETTING_OFF;

	gsKit_init_screen(gsGlobal);	/* Apply settings. */
	gsKit_mode_switch(gsGlobal, GS_ONESHOT);
}

static int LoadFontIntoBuffer(struct gsGlobal *gsGlobal, const char *path)
{
	FILE *file;
	int result, size;
	void *buffer;

	if((file = fopen(path, "rb")) != NULL)
	{
		fseek(file, 0, SEEK_END);
		size = ftell(file);
		rewind(file);

		if((buffer = memalign(64, size)) != NULL)
		{
			if(fread(buffer, 1, size, file) == size)
			{
				if((result = FontInitWithBuffer(gsGlobal, buffer, size)) != 0)
					free(buffer);
				else {
					gFontBuffer = buffer;
					gFontBufferSize = size;
				}
			} else {
				result = -EIO;
				free(buffer);
			}
		} else
			result = -ENOMEM;
		fclose(file);
	} else
		result = -errno;

	return result;
}

static int InitFont(void)
{
	int result;
	char *pFontFilePath;

	if((pFontFilePath=GetFontFilePath(language))!=NULL)
	{
		DEBUG_PRINTF("GetFontFilePath(%d): %s\n", language, pFontFilePath);
	}
	else{
		printf("Can't get font file path from GetFontFilePath(%d).\n", language);
		return -1;
	}

	if((result=FontInit(gsGlobal, pFontFilePath))!=0)
	{
		DEBUG_PRINTF("InitFont(%s) result: %d. Using default font.\n", pFontFilePath, result);
		free(pFontFilePath);
		pFontFilePath=GetDefaultFontFilePath();

		result=FontInit(gsGlobal, pFontFilePath);
	}
	if(result != 0)
		printf("InitFont(%s) error: %d\n", pFontFilePath, result);
	free(pFontFilePath);

	return result;
}

static int InitSubFont(void)
{
	int result;
	char *pFontFilePath;

	if((pFontFilePath = GetFontFilePath(LANGUAGE_JAPANESE)) == NULL)
	{
		printf("Can't get font file path from GetFontFilePath(LANGUAGE_JAPANESE).\n");
		return -1;
	}

	result = AddSubFont(gsGlobal, pFontFilePath);

	if(result != 0)
		printf("InitFont(%s) error: %d\n", pFontFilePath, result);
	free(pFontFilePath);

	return result;
}

static int InitFontWithBuffer(void)
{
	int result;
	char *pFontFilePath;

	if(gFontBuffer == NULL)
	{
		if((pFontFilePath=GetFontFilePath(language))!=NULL)
		{
			DEBUG_PRINTF("GetFontFilePath(%d): %s\n", language, pFontFilePath);
		}
		else{
			printf("Can't get font file path from GetFontFilePath(%d).\n", language);
			return -1;
		}

		if((result=LoadFontIntoBuffer(gsGlobal, pFontFilePath))!=0)
		{
			DEBUG_PRINTF("InitFont(%s) result: %d. Using default font.\n", pFontFilePath, result);
			free(pFontFilePath);
			pFontFilePath=GetDefaultFontFilePath();

			result=LoadFontIntoBuffer(gsGlobal, pFontFilePath);
		}
		if(result != 0)
			printf("InitFont(%s) error: %d\n", pFontFilePath, result);
		free(pFontFilePath);
	} else
		result = 0;

	return result;
}

int ReinitializeUI(void)
{
	SyncFlipFB();	//Redraw the screen to clear gsKit's DMA tag buffer, to avoid a crash if the gsKit_send function is used while that buffer isn't cleared.
	gsKit_vram_clear(gsGlobal);

	UploadTexture(gsGlobal, &BackgroundTexture);
	UploadTexture(gsGlobal, &PadLayoutTexture);
	UploadTexture(gsGlobal, &DeviceIconTexture);
	return FontReset(gsGlobal);
}

int InitializeUI(int BufferFont)
{
	int result;

	result=0;
	if((language=configGetLanguage())>=NUM_SUPPORTED_LANGUAGES) language=LANGUAGE_ENGLISH;
	memset(LangStringWrapTable, 0, sizeof(LangStringWrapTable));

	DEBUG_PRINTF("InitializeUI: language is: %u\n", language);

	if(GetConsoleRegion() == CONSOLE_REGION_JAPAN)
	{
		SelectButton = PAD_CIRCLE;
		CancelButton = PAD_CROSS;
	} else {
		SelectButton = PAD_CROSS;
		CancelButton = PAD_CIRCLE;
	}

	InitGraphics();

	while((result = LoadLanguageStrings(language)) == -ENODEV)
	{
		DEBUG_PRINTF("LoadLanguageStrings(%u): %d\n", language, result);
		WaitForDevice();
	}

	DEBUG_PRINTF("LoadLanguageStrings(%u) result: %d\n", language, result);
	if(result != 0)
	{
		if((result = LoadDefaultLanguageStrings()) != 0)
		{
			DEBUG_PRINTF("LoadDefaultLanguageStrings result: %d\n", result);
			return result;
		}
	}

	result = BufferFont ? InitFontWithBuffer() : InitFont();

	if(result==0)
	{	//Dual-font support for non-Japanese consoles. Even if it cannot be loaded, it is okay.
		if(language != LANGUAGE_JAPANESE)
			InitSubFont();

		//Load common assets.
		BackgroundTexture.Delayed = GS_SETTING_ON;
		LoadBackground(gsGlobal, &BackgroundTexture);
		PadLayoutTexture.Delayed = GS_SETTING_ON;
		LoadPadGraphics(gsGlobal, &PadLayoutTexture);
		DeviceIconTexture.Delayed = GS_SETTING_ON;
		LoadDeviceIcons(gsGlobal, &DeviceIconTexture);

		UploadTexture(gsGlobal, &BackgroundTexture);
		UploadTexture(gsGlobal, &PadLayoutTexture);
		UploadTexture(gsGlobal, &DeviceIconTexture);

		gsKit_clear(gsGlobal, GS_BLACK);
	}

	return result;
}

void DeinitializeUI(void)
{
	UnloadLanguage();
	FontDeinit();

	if(gFontBuffer != NULL)
	{
		free(gFontBuffer);
		gFontBuffer = NULL;
		gFontBufferSize = 0;
	}
}

enum MBOX_SCREEN_ID{
	MBOX_SCREEN_ID_TITLE	= 1,
	MBOX_SCREEN_ID_MESSAGE,
	MBOX_SCREEN_ID_BTN1,
	MBOX_SCREEN_ID_BTN2,
	MBOX_SCREEN_ID_BTN3,
	MBOX_SCREEN_ID_BTN4,
};

static struct UIMenuItem MessageBoxItems[]={
	{MITEM_LABEL, MBOX_SCREEN_ID_TITLE},
	{MITEM_SEPERATOR},
	{MITEM_BREAK},

	{MITEM_STRING, MBOX_SCREEN_ID_MESSAGE, MITEM_FLAG_READONLY}, {MITEM_BREAK}, {MITEM_BREAK},

	{MITEM_BREAK},
	{MITEM_BREAK},
	{MITEM_BREAK},
	{MITEM_BREAK},
	{MITEM_BREAK},
	{MITEM_BREAK},

	{MITEM_BUTTON, MBOX_SCREEN_ID_BTN1, MITEM_FLAG_POS_MID, 0, 16}, {MITEM_BREAK}, {MITEM_BREAK},
	{MITEM_BUTTON, MBOX_SCREEN_ID_BTN3, MITEM_FLAG_POS_MID, 0, 16}, {MITEM_BREAK}, {MITEM_BREAK},
	{MITEM_BUTTON, MBOX_SCREEN_ID_BTN2, MITEM_FLAG_POS_MID, 0, 16}, {MITEM_BREAK}, {MITEM_BREAK},
	{MITEM_BUTTON, MBOX_SCREEN_ID_BTN4, MITEM_FLAG_POS_MID, 0, 16},

	{MITEM_TERMINATOR}
};

static struct UIMenu MessageBoxMenu = {NULL, NULL, MessageBoxItems, {{BUTTON_TYPE_SYS_SELECT, SYS_UI_LBL_OK}, {BUTTON_TYPE_SYS_CANCEL, SYS_UI_LBL_CANCEL}}};

int ShowMessageBox(int Option1Label, int Option2Label, int Option3Label, int Option4Label, const char *message, int title)
{
	short int numButtons;

	UISetLabel(&MessageBoxMenu, MBOX_SCREEN_ID_TITLE, title);
	UISetString(&MessageBoxMenu, MBOX_SCREEN_ID_MESSAGE, message);

	UISetLabel(&MessageBoxMenu, MBOX_SCREEN_ID_BTN1, Option1Label);
	UISetVisible(&MessageBoxMenu, MBOX_SCREEN_ID_BTN1, Option1Label != -1);
	UISetLabel(&MessageBoxMenu, MBOX_SCREEN_ID_BTN2, Option2Label);
	UISetVisible(&MessageBoxMenu, MBOX_SCREEN_ID_BTN2, Option2Label != -1);
	UISetLabel(&MessageBoxMenu, MBOX_SCREEN_ID_BTN3, Option3Label);
	UISetVisible(&MessageBoxMenu, MBOX_SCREEN_ID_BTN3, Option3Label != -1);
	UISetLabel(&MessageBoxMenu, MBOX_SCREEN_ID_BTN4, Option4Label);
	UISetVisible(&MessageBoxMenu, MBOX_SCREEN_ID_BTN4, Option4Label != -1);

	numButtons = 0;
	if(Option1Label != -1)
		numButtons++;
	if(Option2Label != -1)
		numButtons++;
	if(Option3Label != -1)
		numButtons++;
	if(Option4Label != -1)
		numButtons++;

	if(numButtons > 0)
	{
		MessageBoxMenu.hints[0].button = BUTTON_TYPE_SYS_SELECT;
		MessageBoxMenu.hints[1].button = (numButtons == 1) ? -1 : BUTTON_TYPE_SYS_CANCEL;

		switch(UIExecMenu(&MessageBoxMenu, 0, NULL, NULL))
		{
			case MBOX_SCREEN_ID_BTN1:
				return 1;
			case MBOX_SCREEN_ID_BTN2:
				return 2;
			case MBOX_SCREEN_ID_BTN3:
				return 3;
			case MBOX_SCREEN_ID_BTN4:
				return 4;
			default:
				return 0;
		}
	} else {
		MessageBoxMenu.hints[0].button = -1;
		MessageBoxMenu.hints[1].button = -1;
		UIDrawMenu(&MessageBoxMenu, 0, UI_OFFSET_X, UI_OFFSET_Y, -1);
		SyncFlipFB();
	}
}

void DisplayWarningMessage(unsigned int message)
{
	ShowMessageBox(SYS_UI_LBL_OK, -1, -1, -1, GetUIString(message), SYS_UI_LBL_WARNING);
}

void DisplayErrorMessage(unsigned int message)
{
	ShowMessageBox(SYS_UI_LBL_OK, -1, -1, -1, GetUIString(message), SYS_UI_LBL_ERROR);
}

void DisplayInfoMessage(unsigned int message)
{
	ShowMessageBox(SYS_UI_LBL_OK, -1, -1, -1, GetUIString(message), SYS_UI_LBL_INFO);
}

int DisplayPromptMessage(unsigned int message, unsigned char Button1Label, unsigned char Button2Label)
{
	return ShowMessageBox(Button1Label, Button2Label, -1, -1, GetUIString(message), SYS_UI_LBL_CONFIRM);
}

void DisplayFlashStatusUpdate(unsigned int message)
{
	ShowMessageBox(-1, -1, -1, -1, GetUIString(message), SYS_UI_LBL_WAIT);
}

struct UIMenuItem *UIGetItem(struct UIMenu *menu, unsigned char id)
{
	struct UIMenuItem *result;
	unsigned int i;

	result = NULL;
	for(i=0; menu->items[i].type != MITEM_TERMINATOR; i++)
	{
		if(menu->items[i].id == id)
		{
			result = &menu->items[i];
			break;
		}
	}

	return result;
}

void UISetVisible(struct UIMenu *menu, unsigned char id, int visible)
{
	struct UIMenuItem *item;

	if((item = UIGetItem(menu, id)) != NULL)
	{
		if(visible)
			item->flags &= ~MITEM_FLAG_HIDDEN;
		else
			item->flags |= MITEM_FLAG_HIDDEN;
	}
}

void UISetReadonly(struct UIMenu *menu, unsigned char id, int readonly)
{
	struct UIMenuItem *item;

	if((item = UIGetItem(menu, id)) != NULL)
	{
		if(!readonly)
			item->flags &= ~MITEM_FLAG_READONLY;
		else
			item->flags |= MITEM_FLAG_READONLY;
	}
}

void UISetEnabled(struct UIMenu *menu, unsigned char id, int enabled)
{
	struct UIMenuItem *item;

	if((item = UIGetItem(menu, id)) != NULL)
	{
		if(enabled)
			item->flags &= ~MITEM_FLAG_DISABLED;
		else
			item->flags |= MITEM_FLAG_DISABLED;
	}
}

void UISetValue(struct UIMenu *menu, unsigned char id, int value)
{
	struct UIMenuItem *item;

	if((item = UIGetItem(menu, id)) != NULL)
	{
		item->value.value = value;
	}
}

int UIGetValue(struct UIMenu *menu, unsigned char id)
{
	struct UIMenuItem *item;

	if((item = UIGetItem(menu, id)) != NULL)
	{
		return item->value.value;
	}

	return -1;
}

void UISetLabel(struct UIMenu *menu, unsigned char id, int label)
{
	struct UIMenuItem *item;

	if((item = UIGetItem(menu, id)) != NULL)
	{
		item->label.id = label;
	}
}

void UISetString(struct UIMenu *menu, unsigned char id, const char *string)
{
	struct UIMenuItem *item;

	if((item = UIGetItem(menu, id)) != NULL)
	{
		item->string.buffer = string;
	}
}

void UISetType(struct UIMenu *menu, unsigned char id, unsigned char type)
{
	struct UIMenuItem *item;

	if((item = UIGetItem(menu, id)) != NULL)
	{
		item->type = type;
	}
}

void UISetFormat(struct UIMenu *menu, unsigned char id, unsigned char format, unsigned char width)
{
	struct UIMenuItem *item;

	if((item = UIGetItem(menu, id)) != NULL)
	{
		item->format = format;
		item->width = width;
	}
}

void UISetEnum(struct UIMenu *menu, unsigned char id, const int *labels, int count)
{
	struct UIMenuItem *item;

	if((item = UIGetItem(menu, id)) != NULL)
	{
		item->enumeration.labels = labels;
		item->enumeration.count = count;
		item->enumeration.selectedIndex = 0;
	}
}

void UISetEnumSelectedIndex(struct UIMenu *menu, unsigned char id, int selectedIndex)
{
	struct UIMenuItem *item;

	if((item = UIGetItem(menu, id)) != NULL)
	{
		item->enumeration.selectedIndex = selectedIndex;
	}
}

int UIGetEnumSelectedIndex(struct UIMenu *menu, unsigned char id)
{
	struct UIMenuItem *item;

	if((item = UIGetItem(menu, id)) != NULL)
	{
		return item->enumeration.selectedIndex;
	}

	return -1;
}

void UIDrawMenu(struct UIMenu *menu, unsigned short int frame, short int StartX, short int StartY, short int selection)
{
	const char *pLabel;
	char FormatString[8], *pFormatString, ValueString[32];
	struct UIMenuItem *item, *SelectedItem;
	short int x, y, width, height, xRel, yRel, button, i;
	u64 colour;

	DrawBackground(gsGlobal, &BackgroundTexture);

	x = StartX;
	y = StartY;
	SelectedItem = selection >= 0 ? &menu->items[selection] : NULL;
	for(item = menu->items; item->type != MITEM_TERMINATOR; item++)
	{
		if(item->flags&MITEM_FLAG_HIDDEN) continue;

		if(item->flags&MITEM_FLAG_POS_ABS)
		{
			x = item->x;
			y = item->y;
		} else {
			x += item->x;
			y += item->y;
		}

		switch(item->type)
		{
			case MITEM_SEPERATOR:
				x = StartX;
				y += UI_FONT_HEIGHT;
				gsKit_prim_line(gsGlobal, x, y+UI_FONT_HEIGHT/2, gsGlobal->Width - UI_OFFSET_X, y+UI_FONT_HEIGHT/2, 1, GS_WHITE);
				//Fall through.
			case MITEM_BREAK:
				x = StartX;
				y += UI_FONT_HEIGHT;
				break;
			case MITEM_TAB:
				x += (UI_TAB_STOPS*UI_FONT_WIDTH) - (unsigned int)x % (unsigned int)(UI_TAB_STOPS*UI_FONT_WIDTH);
				break;
			case MITEM_SPACE:
				x += UI_FONT_WIDTH;
				break;
			case MITEM_STRING:
				if(item->string.buffer != NULL)
				{
					colour = (item->flags & MITEM_FLAG_DISABLED) ? GS_GREY_FONT : ((item->flags & MITEM_FLAG_READONLY) ? GS_WHITE_FONT : (item == SelectedItem ? GS_YELLOW_FONT : GS_BLUE_FONT));
					FontPrintfWithFeedback(gsGlobal, x, y, 1, 1.0f, colour, item->string.buffer, &xRel, &yRel);
					x += xRel;
					y += yRel;
				}
				break;
			case MITEM_BUTTON:
				if((pLabel = GetUILabel(item->value.value)) != NULL)
				{
					width = item->width * UI_FONT_WIDTH;
					height = UI_FONT_HEIGHT + UI_FONT_HEIGHT / 2;
					if(item == SelectedItem)
					{
						width *= 1.1f;
						height *= 1.1f;
					}

					if(item->flags & MITEM_FLAG_POS_MID)
						x = StartX + (gsGlobal->Width - width) / 2;

					gsKit_prim_sprite(gsGlobal, x, y - UI_FONT_HEIGHT / 2, x + width, y + height, 2, item == SelectedItem ? GS_LGREY : GS_GREY);

					colour = (item->flags & MITEM_FLAG_DISABLED) ? GS_GREY_FONT : (item == SelectedItem ? GS_YELLOW_FONT : GS_WHITE_FONT);

					FontPrintfWithFeedback(gsGlobal, x + (width - item->label.TextWidth) / 2, y, 1, 1.0f, colour, pLabel, &xRel, &yRel);
					item->label.TextWidth = xRel;
					x += xRel;
					y += yRel + UI_FONT_HEIGHT;
				}
				break;
			case MITEM_LABEL:
				if((pLabel = GetUILabel(item->value.value)) != NULL)
				{
					FontPrintfWithFeedback(gsGlobal, x, y, 1, 1.0f, GS_WHITE_FONT, pLabel, &xRel, &yRel);
					x += xRel;
					y += yRel;
				}
				break;
			case MITEM_COLON:
				FontPrintfWithFeedback(gsGlobal, x, y, 1, 1.0f, GS_WHITE_FONT, ":", &xRel, NULL);
				x += xRel;
				break;
			case MITEM_DASH:
				FontPrintfWithFeedback(gsGlobal, x, y, 1, 1.0f, GS_WHITE_FONT, "-", &xRel, NULL);
				x += xRel;
				break;
			case MITEM_DOT:
				FontPrintfWithFeedback(gsGlobal, x, y, 1, 1.0f, GS_WHITE_FONT, ".", &xRel, NULL);
				x += xRel;
				break;
			case MITEM_SLASH:
				FontPrintfWithFeedback(gsGlobal, x, y, 1, 1.0f, GS_WHITE_FONT, "/", &xRel, NULL);
				x += xRel;
				break;
			case MITEM_VALUE:
				pFormatString = FormatString;

				if(item->flags&MITEM_FLAG_UNIT_PREFIX)
				{
					switch(item->format)
					{
						case MITEM_FORMAT_HEX:
							pFormatString[0] = '0';
							pFormatString[1] = 'x';
							pFormatString+=2;
							break;
					}
				}

				*pFormatString = '%';
				pFormatString++;

				if(item->width > 0)
				{
					pFormatString += sprintf(pFormatString, "0%u", item->width);
				}

				switch(item->format)
				{
					case MITEM_FORMAT_DEC:
						*pFormatString = 'd';
						pFormatString++;
						break;
					case MITEM_FORMAT_UDEC:
						*pFormatString = 'u';
						pFormatString++;
						break;
					case MITEM_FORMAT_HEX:
						*pFormatString = 'x';
						pFormatString++;
						break;
					case MITEM_FORMAT_POINTER:
						*pFormatString = 'p';
						pFormatString++;
						break;
					case MITEM_FORMAT_FLOAT:
						*pFormatString = 'f';
						pFormatString++;
						break;
				}

				*pFormatString = '\0';
				sprintf(ValueString, FormatString, item->value.value);
				colour = (item->flags & MITEM_FLAG_DISABLED) ? GS_GREY_FONT : ((item->flags & MITEM_FLAG_READONLY) ? GS_WHITE_FONT : (item == SelectedItem ? GS_YELLOW_FONT : GS_BLUE_FONT));
				FontPrintfWithFeedback(gsGlobal, x, y, 1, 1.0f, colour, ValueString, &xRel, NULL);
				x += xRel;
				break;
			case MITEM_PROGRESS:
				DrawProgressBar(gsGlobal, item->value.value / 100.0f, x + 20, y, 4, gsGlobal->Width - (x + 20) - 20, GS_BLUE);
				y += UI_FONT_HEIGHT;
				break;
			case MITEM_TOGGLE:
				colour = (item->flags & MITEM_FLAG_DISABLED) ? GS_GREY_FONT : ((item->flags & MITEM_FLAG_READONLY) ? GS_WHITE_FONT : (item == SelectedItem ? GS_YELLOW_FONT : GS_BLUE_FONT));
				FontPrintfWithFeedback(gsGlobal, x, y, 1, 1.0f, colour, GetUILabel(item->value.value == 0 ? SYS_UI_LBL_DISABLED : SYS_UI_LBL_ENABLED), &xRel, &yRel);
				x += xRel;
				y += yRel;
				break;
			case MITEM_ENUM:
				if((pLabel = GetUILabel(item->enumeration.labels[item->enumeration.selectedIndex])) != NULL)
				{
					colour = (item->flags & MITEM_FLAG_DISABLED) ? GS_GREY_FONT : ((item->flags & MITEM_FLAG_READONLY) ? GS_WHITE_FONT : (item == SelectedItem ? GS_YELLOW_FONT : GS_BLUE_FONT));
					FontPrintfWithFeedback(gsGlobal, x, y, 1, 1.0f, colour, pLabel, &xRel, &yRel);
					x += xRel;
					y += yRel;
				}
		}
	}

	//Draw legends
	if(menu->next != NULL)
	{
		if(frame % 180 < 30)
			DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_R1, gsGlobal->Width - 40 - frame % 30 / 3, 400, 4);
		else if(frame % 180 < 60)
			DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_R1, gsGlobal->Width - 40 - 10 + (frame % 30 / 3), 400, 4);
		else
			DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_R1, gsGlobal->Width - 40, 400, 4);
	}
	if(menu->prev != NULL)
	{
		if(frame % 180 < 30)
			DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_L1, 20 + frame % 30 / 3, 400, 4);
		else if(frame % 180 < 60)
			DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_L1, 20 + 10 - frame % 30 / 3, 400, 4);
		else
			DrawButtonLegend(gsGlobal, &PadLayoutTexture, BUTTON_TYPE_L1, 20, 400, 4);
	}

	x = 64;
	for(i = 0; i < 2; i++)
	{
		if(menu->hints[i].button >= 0)
		{
			switch(menu->hints[i].button)
			{
				case BUTTON_TYPE_SYS_SELECT:
					button = SelectButton == PAD_CIRCLE ? BUTTON_TYPE_CIRCLE : BUTTON_TYPE_CROSS;
					break;
				case BUTTON_TYPE_SYS_CANCEL:
					button = CancelButton == PAD_CIRCLE ? BUTTON_TYPE_CIRCLE : BUTTON_TYPE_CROSS;
					break;
				default:
					button = menu->hints[i].button;
			}

			DrawButtonLegendWithFeedback(gsGlobal, &PadLayoutTexture, button, x, 420, 4, &xRel);
			x += xRel + 8;
			FontPrintfWithFeedback(gsGlobal, x, 422, 1, 1.0f, GS_WHITE_FONT, GetUILabel(menu->hints[i].label), &xRel, NULL);
			x += xRel + 8;
		}
	}
}

static void UITransitionSlideRightOut(struct UIMenu *menu, int SelectedOption)
{
	int i;

	for(i=0; i<=15; i++)
	{
		UIDrawMenu(menu, i, UI_OFFSET_X + i * 48, UI_OFFSET_Y, SelectedOption);
		SyncFlipFB();
	}
}

static void UITransitionSlideLeftOut(struct UIMenu *menu, int SelectedOption)
{
	int i;

	for(i=0; i<=15; i++)
	{
		UIDrawMenu(menu, i, UI_OFFSET_X + -i * 48, UI_OFFSET_Y, SelectedOption);
		SyncFlipFB();
	}
}

static void UITransitionSlideRightIn(struct UIMenu *menu, int SelectedOption)
{
	int i;

	for(i=15; i>0; i--)
	{
		UIDrawMenu(menu, i, UI_OFFSET_X + i * 48, UI_OFFSET_Y, SelectedOption);
		SyncFlipFB();
	}
}

static void UITransitionSlideLeftIn(struct UIMenu *menu, int SelectedOption)
{
	int i;

	for(i=15; i>0; i--)
	{
		UIDrawMenu(menu, i, UI_OFFSET_X + -i * 48, UI_OFFSET_Y, SelectedOption);
		SyncFlipFB();
	}
}

static void UITransitionFadeIn(struct UIMenu *menu, int SelectedOption)
{
	int i;
	u64 rgba;

	for(i=0; i<=15; i++)
	{
		rgba = GS_SETREG_RGBA(0, 0, 0, 0x80-(i*8));
		UIDrawMenu(menu, i, UI_OFFSET_X, UI_OFFSET_Y, SelectedOption);
		SyncFlipFB();
	}
}

static void UITransitionFadeOut(struct UIMenu *menu, int SelectedOption)
{
	int i;
	u64 rgba;

	for(i=15; i>0; i--)
	{
		rgba = GS_SETREG_RGBA(0, 0, 0, 0x80-(i*8));
		UIDrawMenu(menu, i, UI_OFFSET_X, UI_OFFSET_Y, SelectedOption);
		SyncFlipFB();
	}
}

void UITransition(struct UIMenu *menu, int type, int SelectedOption)
{
	switch(type)
	{
		case UIMT_LEFT_OUT:
			UITransitionSlideLeftOut(menu, SelectedOption);
			break;
		case UIMT_RIGHT_OUT:
			UITransitionSlideRightOut(menu, SelectedOption);
			break;
		case UIMT_LEFT_IN:
			UITransitionSlideLeftIn(menu, SelectedOption);
			break;
		case UIMT_RIGHT_IN:
			UITransitionSlideRightIn(menu, SelectedOption);
			break;
		case UIMT_FADE_IN:
			UITransitionFadeIn(menu, SelectedOption);
			break;
		case UIMT_FADE_OUT:
			UITransitionFadeOut(menu, SelectedOption);
			break;
	}
}

static short int UIGetSelectableItem(struct UIMenu *menu, short int id)
{
	short int result;
	int i;

	result = -1;
	for(i = 0; menu->items[i].type != MITEM_TERMINATOR; i++)
	{
		if(menu->items[i].id == id)
		{
			if(menu->items[i].type > MITEM_LABEL
				&& !(menu->items[i].flags & MITEM_FLAG_DISABLED)
				&& !(menu->items[i].flags & MITEM_FLAG_HIDDEN)
				&& !(menu->items[i].flags & MITEM_FLAG_READONLY))
					result = i;

			break;
		}
	}

	return result;
}

static short int UIGetNextSelectableItem(struct UIMenu *menu, short int index)
{
	short int result;
	int i;

	index = (index < 0) ? 0 : index + 1;
	result = -1;
	for(i = index; menu->items[i].type != MITEM_TERMINATOR; i++)
	{
		if(menu->items[i].type > MITEM_LABEL
			&& !(menu->items[i].flags & MITEM_FLAG_DISABLED)
			&& !(menu->items[i].flags & MITEM_FLAG_HIDDEN)
			&& !(menu->items[i].flags & MITEM_FLAG_READONLY))
		{
			result = i;
			break;
		}
	}

	return result;
}

static short int UIGetPrevSelectableItem(struct UIMenu *menu, short int index)
{
	short int result;
	int i;

	index = (index < 0) ? 0 : index - 1;
	result = -1;
	for(i = index; i >= 0; i--)
	{
		if(menu->items[i].type > MITEM_LABEL
			&& !(menu->items[i].flags & MITEM_FLAG_DISABLED)
			&& !(menu->items[i].flags & MITEM_FLAG_HIDDEN)
			&& !(menu->items[i].flags & MITEM_FLAG_READONLY))
		{
			result = i;
			break;
		}
	}

	return result;
}

int UIExecMenu(struct UIMenu *FirstMenu, short int SelectedItem, struct UIMenu **CurrentMenu, int (*callback)(struct UIMenu *menu, unsigned short int frame, int selection, u32 padstatus))
{
	struct UIMenu *menu;
	int result;
	u32 PadStatus;
	struct UIMenuItem *item;
	short int selection, NextSel;
	unsigned short int frame;
	u32 PadRepeatStatus, PadRepeatStatusOld, PadStatusTemp;
	unsigned short int PadRepeatDelayTicks, PadRepeatRateTicks;

	PadStatus = 0;
	PadRepeatStatusOld = 0;
	PadRepeatDelayTicks = UI_PAD_REPEAT_START_DELAY;
	PadRepeatRateTicks = UI_PAD_REPEAT_DELAY;
	frame = 0;
	menu = FirstMenu;

	if((selection = UIGetSelectableItem(menu, SelectedItem)) >= 0)
	{	//If the item selected is specified, select it.
		item = &menu->items[selection];
	} else
		//Find first selectable option.
		item = ((selection = UIGetNextSelectableItem(menu, -1)) >= 0) ? &menu->items[selection] : NULL;

	if(callback != NULL)
	{
		if((result = callback(menu, frame, selection, 0)) != 0)
			goto exit_menu;
	}

	while(1)
	{
		PadStatus = ReadCombinedPadStatus();

		//For the pad repeat delay effect.
		PadRepeatStatus = ReadCombinedPadStatus_raw();
		if(PadRepeatStatus == 0 || ((PadRepeatStatusOld != 0) && (PadRepeatStatus != PadRepeatStatusOld)))
		{
			PadRepeatDelayTicks = UI_PAD_REPEAT_START_DELAY;
			PadRepeatRateTicks = UI_PAD_REPEAT_DELAY;

			PadStatusTemp = PadRepeatStatus & ~PadRepeatStatusOld;
			PadRepeatStatusOld = PadRepeatStatus;
			PadRepeatStatus = PadStatusTemp;
		}
		else
		{
			if(PadRepeatDelayTicks == 0)
			{
				//Allow the pad presses to repeat, but only after the pad repeat delay expires.
				if(PadRepeatRateTicks == 0)
				{
					PadRepeatRateTicks = UI_PAD_REPEAT_DELAY;
				}
				else
				{
					PadStatusTemp = PadRepeatStatus & ~PadRepeatStatusOld;
					PadRepeatStatusOld = PadRepeatStatus;
					PadRepeatStatus = PadStatusTemp;
				}

				PadRepeatRateTicks--;
			}
			else
			{
				PadStatusTemp = PadRepeatStatus & ~PadRepeatStatusOld;
				PadRepeatStatusOld = PadRepeatStatus;
				PadRepeatStatus = PadStatusTemp;

				PadRepeatDelayTicks--;
			}
		}

		if(PadRepeatStatus&PAD_UP)
		{
			//Try to find the previous selectable option.
			if((NextSel = UIGetPrevSelectableItem(menu, selection)) >= 0)
			{
				selection = NextSel;
				item = &menu->items[selection];
			}
		} else if(PadRepeatStatus&PAD_DOWN) {
			//Try to find the next selectable option.
			if((NextSel = UIGetNextSelectableItem(menu, selection)) >= 0)
			{
				selection = NextSel;
				item = &menu->items[selection];
			}
		} else if(PadRepeatStatus&PAD_LEFT) {
			if(item != NULL && !(item->flags & MITEM_FLAG_READONLY) && !(item->flags & MITEM_FLAG_DISABLED))
			{
				switch(item->type)
				{
					case MITEM_VALUE:
						if(item->value.value - 1 >= item->value.min)
							item->value.value--;
						else
							item->value.value = item->value.max;
						break;
					case MITEM_TOGGLE:
						item->value.value = !item->value.value;
						break;
					case MITEM_ENUM:
						if(item->enumeration.selectedIndex > 0)
							item->enumeration.selectedIndex--;
						else
							item->enumeration.selectedIndex = item->enumeration.count - 1;
						break;
				}
			}
		} else if(PadRepeatStatus&PAD_RIGHT) {
			if(item != NULL && !(item->flags & MITEM_FLAG_READONLY) && !(item->flags & MITEM_FLAG_DISABLED))
			{
				switch(item->type)
				{
					case MITEM_VALUE:
						if(item->value.value + 1 <= item->value.max)
							item->value.value++;
						else
							item->value.value = item->value.min;
						break;
					case MITEM_TOGGLE:
						item->value.value = !item->value.value;
						break;
					case MITEM_ENUM:
						if(item->enumeration.selectedIndex + 1 < item->enumeration.count)
							item->enumeration.selectedIndex++;
						else
							item->enumeration.selectedIndex = 0;
						break;
				}
			}
		}

		if(PadStatus&SelectButton)
		{
			if(item != NULL && !(item->flags & MITEM_FLAG_READONLY) && !(item->flags & MITEM_FLAG_DISABLED))
			{
				switch(item->type)
				{
					case MITEM_BUTTON:
						result = item->id;
						goto exit_menu;
					case MITEM_TOGGLE:
						item->value.value = !item->value.value;
						break;
				}
			}
		} else if(PadStatus&CancelButton) {
			//User aborted.
			result = 1;
			break;
		}

		if(PadStatus&PAD_R1) {
			if(menu->next != NULL)
			{
				UITransition(menu, UIMT_RIGHT_OUT, selection);

				menu = menu->next;
				//Find first selectable option.
				item = ((selection = UIGetNextSelectableItem(menu, -1)) >= 0) ? &menu->items[selection] : NULL;

				UITransition(menu, UIMT_LEFT_IN, selection);

				if(callback != NULL)
				{
					if((result = callback(menu, frame, selection, -1)) != 0)
						break;
				}
			}
		} else if(PadStatus&PAD_L1) {
			if(menu->prev != NULL)
			{
				UITransition(menu, UIMT_LEFT_OUT, selection);

				menu = menu->prev;
				//Find first selectable option.
				item = ((selection = UIGetNextSelectableItem(menu, -1)) >= 0) ? &menu->items[selection] : NULL;

				UITransition(menu, UIMT_RIGHT_IN, selection);

				if(callback != NULL)
				{
					if((result = callback(menu, frame, selection, -1)) != 0)
						break;
				}
			}
		}

		UIDrawMenu(menu, frame, UI_OFFSET_X, UI_OFFSET_Y, selection);

		if(callback != NULL)
		{
			if((result = callback(menu, frame, selection, PadStatus)) != 0)
				break;
		}

		SyncFlipFB();
		frame++;
	}

exit_menu:
	if (CurrentMenu != NULL)
		*CurrentMenu = menu;

	return result;
}
