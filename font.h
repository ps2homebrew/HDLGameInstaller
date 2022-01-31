#define FNT_ATLAS_WIDTH		128
#define FNT_ATLAS_HEIGHT	128
#define FNT_MAX_ATLASES		4
#define FNT_CHAR_WIDTH		16
#define FNT_CHAR_HEIGHT		16
#define FNT_TAB_STOPS		4

int FontInit(GSGLOBAL *gsGlobal, const char *FontFile);
int FontInitWithBuffer(GSGLOBAL *gsGlobal, void *buffer, unsigned int size);
int AddSubFont(GSGLOBAL *gsGlobal, const char *FontFile);
int AddSubFontWithBuffer(GSGLOBAL *gsGlobal, void *buffer, unsigned int size);
void FontDeinit(void);
int FontReset(GSGLOBAL *gsGlobal);	//Performs a partial re-initialization of the Font library and re-allocates VRAM. Used when VRAM has been cleared.
void FontPrintfWithFeedback(GSGLOBAL *gsGlobal, short int x, short int y, short int z, float scale, u64 colour, const char *string, short int *xRel, short int *yRel);
void FontPrintf(GSGLOBAL *gsGlobal, short int x, short int y, short int z, float scale, u64 colour, const char *string);
void FontGetPrintPos(GSGLOBAL *gsGlobal, short int *x_rel, short int *y_rel, float scale, const char *string, int pos);
int FontGetGlyphWidth(GSGLOBAL *gsGlobal, wint_t character);
void wFontPrintfWithFeedback(GSGLOBAL *gsGlobal, short x, short int y, short int z, float scale, u64 colour, const wchar_t *string, short int *xRel, short int *yRel);
void wFontPrintf(GSGLOBAL *gsGlobal, short int x, short int y, short int z, float scale, u64 colour, const wchar_t *string);
int wFontPrintField(GSGLOBAL *gsGlobal, short int x, short int y, short int z, float scale, u64 colour, const wchar_t *string, unsigned int spacePix, int cursor);
int wFontPrintTitle(GSGLOBAL *gsGlobal, short int x, short int y, short int z, float scale, u64 colour, const wchar_t *string, unsigned int spacePix);
