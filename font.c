/*	Font-drawing engine
	Version:	1.22 (gsKit version)
	Last updated:	2018/12/08	*/

#include <malloc.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <kernel.h>
#include <wchar.h>

#include <gsKit.h>
#include <gsInline.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H

#include "graphics.h"
#include "font.h"

extern GSGLOBAL *gsGlobal;

/*	Draw the glyphs as close as possible to each other, to save VRAM.

	C: character
	X: Horizontal frontier
	Y: Vertical frontier
	I: Initial state, no frontier.

	Initial state:
		IIII
		IIII
		IIII
		IIII

	After one character is added to an empty atlas, initialize the X and Y frontier.
		CXXX
		YXXX
		YXXX
		YXXX

	After one more character is added:
		CCXX
		YYXX
		YYXX
		YYXX

	After one more character is added:
		CCCX
		YYYX
		YYYX
		YYYX

	After one more character is added:
		CCCC
		YYYY
		YYYY
		YYYY

	After one more character is added:
		CCCC
		CXXX
		YXXX
		YXXX	*/

struct FontGlyphSlotInfo {
	struct FontGlyphSlot *slot;
	u32 vram;	//Address of the atlas buffer in VRAM
};

struct FontGlyphSlot{
	wint_t character;
	unsigned short int VramPageX, VramPageY;
	short int top, left;	//Offsets to place the glyph at. Refer to the FreeType documentation.
	short int advance_x, advance_y;
	unsigned short int width, height;
};

struct FontFrontier{
	short int x, y;
	short int width, height;
};

struct FontAtlas{
	u32 vram;	//Address of the buffer in VRAM
	void *buffer;	//Address of the buffer in local memory

	unsigned int NumGlyphs;
	struct FontGlyphSlot *GlyphSlot;
	struct FontFrontier frontier[2];
};

typedef struct Font{
	FT_Face FTFace;
	GSTEXTURE Texture;
	unsigned short int IsLoaded;
	unsigned short int IsInit;
	struct FontAtlas atlas[FNT_MAX_ATLASES];
} Font_t;

static FT_Library FTLibrary;
static Font_t GS_FTFont;
static Font_t GS_sub_FTFont;

static int ResetThisFont(GSGLOBAL *gsGlobal, Font_t *font)
{
	struct FontAtlas *atlas;
	unsigned short int i;
	int result;

	result=0;

	font->Texture.VramClut = gsKit_vram_alloc(gsGlobal, gsKit_texture_size(16, 16, font->Texture.ClutPSM), GSKIT_ALLOC_USERBUFFER);

	if(font->Texture.VramClut != GSKIT_ALLOC_ERROR)
	{
		gsKit_texture_send_inline(gsGlobal,
			font->Texture.Clut,
			16,
			16,
			font->Texture.VramClut,
			font->Texture.ClutPSM,
			1,
			GS_CLUT_PALLETE);

		for(i=0,atlas=font->atlas; i<FNT_MAX_ATLASES; i++,atlas++)
		{
			atlas->frontier[0].width = 0;
			atlas->frontier[0].height = 0;
			atlas->frontier[1].width = 0;
			atlas->frontier[1].height = 0;
			atlas->vram = GSKIT_ALLOC_ERROR;
			if(atlas->GlyphSlot != NULL)
			{
				free(atlas->GlyphSlot);
				atlas->GlyphSlot = NULL;
			}
			if(atlas->buffer != NULL)
			{
				free(atlas->buffer);
				atlas->buffer = NULL;
			}
			atlas->NumGlyphs = 0;
		}
	} else {
		printf("Font: error - unable to allocate VRAM for CLUT.\n");
		result = -1;
	}

	return result;
}

int FontReset(GSGLOBAL *gsGlobal)
{
	int result;

	if((result = ResetThisFont(gsGlobal, &GS_FTFont)) == 0)
		result = ResetThisFont(gsGlobal, &GS_sub_FTFont);

	return 0;
}

static int InitFontSupportCommon(GSGLOBAL *gsGlobal, Font_t *font)
{
	int result;
	unsigned short int i;

	if((result=FT_Set_Pixel_Sizes(font->FTFace, FNT_CHAR_WIDTH, FNT_CHAR_HEIGHT))==0)
	{
		font->Texture.Width = FNT_ATLAS_WIDTH;
		font->Texture.Height = FNT_ATLAS_HEIGHT;
		font->Texture.PSM = GS_PSM_T8;
		font->Texture.ClutPSM = GS_PSM_CT32;
		font->Texture.Filter=GS_FILTER_LINEAR;
		font->Texture.Delayed=GS_SETTING_ON;

		// generate the clut table
		u32 *clut = memalign(64, 256 * 4);
		font->Texture.Clut=clut;
		for (i = 0; i < 256; ++i) clut[i] = (i > 0x80 ? 0x80 : i) << 24 | i << 16 | i << 8 | i;
		SyncDCache(clut, (void*)((unsigned int)clut+256*4));

		gsKit_setup_tbw(&font->Texture);

		if(!font->IsInit)
			ResetThisFont(gsGlobal, font);
		font->IsLoaded=1;
		font->IsInit=1;
	}

	return result;
}

int FontInit(GSGLOBAL *gsGlobal, const char *FontFile)
{
	int result;

	if((result=FT_Init_FreeType(&FTLibrary))==0)
	{
		if((result=FT_New_Face(FTLibrary, FontFile, 0, &GS_FTFont.FTFace))==0)
			result=InitFontSupportCommon(gsGlobal, &GS_FTFont);

		if(result!=0)
			FontDeinit();
	}

	return result;
}

int FontInitWithBuffer(GSGLOBAL *gsGlobal, void *buffer, unsigned int size)
{
	int result;

	if((result=FT_Init_FreeType(&FTLibrary))==0)
	{
		if((result=FT_New_Memory_Face(FTLibrary, buffer, size, 0, &GS_FTFont.FTFace))==0)
			result=InitFontSupportCommon(gsGlobal, &GS_FTFont);

		if(result!=0)
			FontDeinit();
	}

	return result;
}

static void UnloadFont(Font_t *font)
{
	struct FontAtlas *atlas;
	unsigned int i;

	if(font->IsLoaded)
		FT_Done_Face(font->FTFace);

	for(i=0,atlas=font->atlas; i<FNT_MAX_ATLASES; i++,atlas++)
	{
		if(atlas->buffer!=NULL)
		{
			free(atlas->buffer);
			atlas->buffer = NULL;
		}
		if(atlas->GlyphSlot!=NULL)
		{
			free(atlas->GlyphSlot);
			atlas->GlyphSlot = NULL;
		}
	}
	font->atlas->NumGlyphs = 0;

	if(font->Texture.Clut != NULL)
	{
		free(font->Texture.Clut);
		font->Texture.Clut = NULL;
	}

	font->IsLoaded = 0;
}

int AddSubFont(GSGLOBAL *gsGlobal, const char *FontFile)
{
	int result;

	if((result = FT_New_Face(FTLibrary, FontFile, 0, &GS_sub_FTFont.FTFace)) == 0)
		result = InitFontSupportCommon(gsGlobal, &GS_sub_FTFont);

	if(result != 0)
		UnloadFont(&GS_sub_FTFont);

	return result;
}

int AddSubFontWithBuffer(GSGLOBAL *gsGlobal, void *buffer, unsigned int size)
{
	int result;

	if((result = FT_New_Memory_Face(FTLibrary, buffer, size, 0, &GS_sub_FTFont.FTFace)) == 0)
		result = InitFontSupportCommon(gsGlobal, &GS_sub_FTFont);

	if(result != 0)
		UnloadFont(&GS_sub_FTFont);

	return result;
}

void FontDeinit(void)
{
	UnloadFont(&GS_FTFont);
	UnloadFont(&GS_sub_FTFont);
}

static int AtlasInit(Font_t *font, struct FontAtlas *atlas)
{
	unsigned int TextureSizeEE, i;
	u32 VramTextureSize;
	short int width_aligned, height_aligned;
	int result;

	result = 0;
	width_aligned = (font->Texture.Width+127)&~127;
	height_aligned = (font->Texture.Height+127)&~127;
	VramTextureSize = gsKit_texture_size(width_aligned, height_aligned, font->Texture.PSM);

	if((atlas->vram = gsKit_vram_alloc(gsGlobal, VramTextureSize, GSKIT_ALLOC_USERBUFFER)) != GSKIT_ALLOC_ERROR)
	{
		TextureSizeEE = width_aligned * height_aligned;
		if((atlas->buffer = memalign(64, TextureSizeEE)) == NULL)
		{
			printf("Font: error - unable to allocate memory for atlas.\n");
			result = -ENOMEM;
		}
		memset(atlas->buffer, 0, TextureSizeEE);
		SyncDCache(atlas->buffer, atlas->buffer+TextureSizeEE);
	}
	else
	{
		printf("Font: error - unable to allocate VRAM for atlas.\n");
		result = -ENOMEM;
	}

	return result;
}

static struct FontGlyphSlot *AtlasAlloc(Font_t *font, struct FontAtlas *atlas, short int width, short int height)
{
	struct FontGlyphSlot *GlyphSlot;

	GlyphSlot = NULL;
	if(atlas->buffer == NULL)
	{	//No frontier (empty atlas)
		if(AtlasInit(font, atlas) != 0)
			return NULL;

		//Give the glyph 1px more, for texel rendering.
		atlas->frontier[0].width = FNT_ATLAS_WIDTH - (width + 1);
		atlas->frontier[0].height = FNT_ATLAS_HEIGHT;
		atlas->frontier[0].x = width + 1;
		atlas->frontier[0].y = 0;
		atlas->frontier[1].width = FNT_ATLAS_WIDTH;
		atlas->frontier[1].height = FNT_ATLAS_HEIGHT - (height + 1);
		atlas->frontier[1].x = 0;
		atlas->frontier[1].y = height + 1;

		atlas->NumGlyphs++;

		if((atlas->GlyphSlot = realloc(atlas->GlyphSlot, atlas->NumGlyphs * sizeof(struct FontGlyphSlot))) != NULL)
		{
			GlyphSlot = &atlas->GlyphSlot[atlas->NumGlyphs - 1];

			GlyphSlot->VramPageX = 0;
			GlyphSlot->VramPageY = 0;
		} else {
			printf("Font: error - unable to allocate a new glyph slot.\n");
			atlas->NumGlyphs = 0;
		}

		return GlyphSlot;
	} else {	//We have the frontiers
		//Try to allocate from the horizontal frontier first.
		if((atlas->frontier[0].width >= width + 1)
		   && (atlas->frontier[0].height >= height + 1))
		{
			atlas->NumGlyphs++;

			if((atlas->GlyphSlot = realloc(atlas->GlyphSlot, atlas->NumGlyphs * sizeof(struct FontGlyphSlot))) != NULL)
			{
				GlyphSlot = &atlas->GlyphSlot[atlas->NumGlyphs - 1];

				GlyphSlot->VramPageX = atlas->frontier[0].x;
				GlyphSlot->VramPageY = atlas->frontier[0].y;

				//Give the glyph 1px more, for texel rendering.
				//Update frontier.
				atlas->frontier[0].width -= width + 1;
				atlas->frontier[0].x += width + 1;

				//If the new glyph is a little taller than the glyphs under the horizontal frontier, move the vertical frontier.
				if(atlas->frontier[0].y + height + 1 > atlas->frontier[1].y)
					atlas->frontier[1].y = atlas->frontier[0].y + height + 1;
			} else {
				printf("Font: error - unable to allocate a new glyph slot.\n");
				atlas->NumGlyphs = 0;
			}
		//Now try the vertical frontier.
		} else if((atlas->frontier[1].width >= width + 1)
			  && (atlas->frontier[1].height >= height + 1))
		{
			atlas->NumGlyphs++;

			if((atlas->GlyphSlot = realloc(atlas->GlyphSlot, atlas->NumGlyphs * sizeof(struct FontGlyphSlot))) != NULL)
			{
				GlyphSlot = &atlas->GlyphSlot[atlas->NumGlyphs - 1];

				GlyphSlot->VramPageX = atlas->frontier[1].x;
				GlyphSlot->VramPageY = atlas->frontier[1].y;

				//Give the glyph 1px more, for texel rendering.
				/*	Update frontier.
					If we got here, it means that the horizontal frontier is very close the edge of VRAM.
					Give a large portion of the space recorded under this frontier to the horizontal frontier.

					Before:		After one more character is added:
						CCCC		CCCC
						YYYY		CXXX
						YYYY		YXXX
						YYYY		YXXX	*/
				atlas->frontier[0].x = width + 1;
				atlas->frontier[0].y = atlas->frontier[1].y;
				atlas->frontier[0].width = FNT_ATLAS_WIDTH - (width + 1);
				atlas->frontier[0].height = atlas->frontier[1].height;
				atlas->frontier[1].height -= height + 1;
				atlas->frontier[1].y += height + 1;
			} else {
				printf("Font: error - unable to allocate a new glyph slot.\n");
				atlas->NumGlyphs = 0;
			}
		}
	}

	return GlyphSlot;
}

static void AtlasCopyFT(Font_t *font, struct FontAtlas *atlas, struct FontGlyphSlot *GlyphSlot, FT_GlyphSlot FT_GlyphSlot)
{
	short int yOffset;
	unsigned char *FTCharRow;

	for(yOffset=0; yOffset<FT_GlyphSlot->bitmap.rows; yOffset++)
	{
		FTCharRow=(void*)UNCACHED_SEG(((unsigned int)atlas->buffer+GlyphSlot->VramPageX+(GlyphSlot->VramPageY+yOffset)*font->Texture.Width));
		memcpy(FTCharRow, &((unsigned char*)FT_GlyphSlot->bitmap.buffer)[yOffset*FT_GlyphSlot->bitmap.width], FT_GlyphSlot->bitmap.width);
	}
}

static struct FontGlyphSlot *UploadGlyph(GSGLOBAL *gsGlobal, Font_t *font, wint_t character, FT_GlyphSlot FT_GlyphSlot, struct FontAtlas **AtlasOut)
{
	u64* p_data;
	struct FontAtlas *atlas;
	struct FontGlyphSlot *GlyphSlot;
	unsigned short int i;

	*AtlasOut=NULL;
	GlyphSlot = NULL;
	for(i = 0,atlas=font->atlas; i < FNT_MAX_ATLASES; i++,atlas++)
	{
		if((GlyphSlot = AtlasAlloc(font, atlas, FT_GlyphSlot->bitmap.width, FT_GlyphSlot->bitmap.rows)) != NULL)
			break;
	}

	if(GlyphSlot != NULL)
	{
		GlyphSlot->character = character;
		GlyphSlot->left = FT_GlyphSlot->bitmap_left;
		GlyphSlot->top = FT_GlyphSlot->bitmap_top;
		GlyphSlot->width = FT_GlyphSlot->bitmap.width;
		GlyphSlot->height = FT_GlyphSlot->bitmap.rows;
		GlyphSlot->advance_x = FT_GlyphSlot->advance.x >> 6;
		GlyphSlot->advance_y = FT_GlyphSlot->advance.y >> 6;

		*AtlasOut = atlas;

		//Initiate a texture flush before reusing the VRAM page, if the slot was just used earlier.
		p_data = gsKit_heap_alloc(gsGlobal, 1, 16, GIF_AD);
		*p_data++ = GIF_TAG( 1, 1, 0, 0, 0, 1 );
		*p_data++ = GIF_AD;
		*p_data++ = 0;
		*p_data++ = GS_TEXFLUSH;

		AtlasCopyFT(font, atlas, GlyphSlot, FT_GlyphSlot);
		gsKit_texture_send_inline(gsGlobal,
				atlas->buffer,
				font->Texture.Width,
				font->Texture.Height,
				atlas->vram,
				font->Texture.PSM,
				font->Texture.TBW,
				GS_CLUT_TEXTURE);
	} else
		printf("Font: error - all atlas are full.\n");

	return GlyphSlot;
}

static int GetGlyph(GSGLOBAL *gsGlobal, Font_t *font, wint_t character, int DrawMissingGlyph, struct FontGlyphSlotInfo *glyphInfo)
{
	int i, slot;
	struct FontAtlas *atlas;
	struct FontGlyphSlot *glyphSlot;
	FT_UInt glyphIndex;

	//Scan through all uploaded glyph slots.
	for(i=0,atlas=font->atlas; i<FNT_MAX_ATLASES; i++,atlas++)
	{
		for(slot=0; slot < atlas->NumGlyphs; slot++)
		{
			if(atlas->GlyphSlot[slot].character==character)
			{
				glyphInfo->slot = &atlas->GlyphSlot[slot];
				glyphInfo->vram = atlas->vram;
				return 0;
			}
		}
	}

	//Not in VRAM? Upload it.
	if((glyphIndex = FT_Get_Char_Index(font->FTFace, character)) != 0 || DrawMissingGlyph)
	{
		if(FT_Load_Glyph(font->FTFace, glyphIndex, FT_LOAD_RENDER))
			return -1;

		if((glyphSlot = UploadGlyph(gsGlobal, font, character, font->FTFace->glyph, &atlas)) == NULL)
			return -1;

//		printf("Uploading %c, %u, %u\n", character, GlyphSlot->VramPageX, GlyphSlot->VramPageY);

		glyphInfo->slot = glyphSlot;
		glyphInfo->vram = atlas->vram;
		return 0;
	} else //Otherwise, the glyph is missing from font
		return 1;

	return -1;
}

static int DrawGlyph(GSGLOBAL *gsGlobal, Font_t *font, wint_t character, short int x, short int y, short int z, float scale, u64 colour, int DrawMissingGlyph, short int *width)
{
	struct FontGlyphSlot *glyphSlot;
	struct FontGlyphSlotInfo glyphInfo;
	struct FontAtlas *atlas;
	unsigned short int i, slot;
	short int XCoordinates, YCoordinates;
	FT_UInt GlyphIndex;
	int result;

	if(font->IsLoaded)
	{
		if ((result = GetGlyph(gsGlobal, font, character, DrawMissingGlyph, &glyphInfo)) != 0)
			return result;

		glyphSlot = glyphInfo.slot;
		font->Texture.Vram = glyphInfo.vram;

		YCoordinates=y+(FNT_CHAR_HEIGHT-glyphSlot->top)*scale;
		XCoordinates=x+glyphSlot->left*scale;

		//To centre the texels, add 0.5 to the coordinates.
		gsKit_prim_sprite_texture(gsGlobal, &font->Texture,
						XCoordinates, YCoordinates,									//x1, y1
						glyphSlot->VramPageX + 0.5f, glyphSlot->VramPageY + 0.5f,					//u1, v1
						XCoordinates+glyphSlot->width*scale, YCoordinates+glyphSlot->height*scale,			//x2, y2
						glyphSlot->VramPageX+glyphSlot->width + 0.5f, glyphSlot->VramPageY+glyphSlot->height + 0.5f,	//u2, v2
						z, colour);

		*width = glyphSlot->advance_x * scale;
	} else	//Not loaded
		return -1;

	return 0;
}

void FontPrintfWithFeedback(GSGLOBAL *gsGlobal, short x, short int y, short int z, float scale, u64 colour, const char *string, short int *xRel, short int *yRel)
{
	wchar_t wchar;
	short int StartX, StartY, width;
	int charsize, bufmax;

	u64 oldalpha = gsGlobal->PrimAlpha;
	unsigned char oldpabe = gsGlobal->PABE;
	gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0,1,0,1,0), GS_SETTING_OFF);

	StartX = x;
	StartY = y;

	for(bufmax = strlen(string) + 1; *string != '\0'; string += charsize, bufmax -= charsize)
	{
		//Up to MB_CUR_MAX
		charsize = mbtowc(&wchar, string, bufmax);

		switch(wchar)
		{
			case '\r':
				x=StartX;
				break;
			case '\n':
				y+=(short int)(FNT_CHAR_HEIGHT*scale);
				x=StartX;
				break;
			case '\t':
				x+=(short int)(FNT_TAB_STOPS*FNT_CHAR_WIDTH*scale)-(unsigned int)x%(unsigned int)(FNT_TAB_STOPS*FNT_CHAR_WIDTH*scale);
				break;
			default:
				width = 0;
				if(DrawGlyph(gsGlobal, &GS_FTFont, wchar, x, y, z, scale, colour, 0, &width) != 0)
				{
					if(DrawGlyph(gsGlobal, &GS_sub_FTFont, wchar, x, y, z, scale, colour, 0, &width) != 0)
					{	//Cannot locate the glyph, so draw the missing glyph character.
						DrawGlyph(gsGlobal, &GS_FTFont, wchar, x, y, z, scale, colour, 1, &width);
					}
				}

				x += width;
		}
	}

	gsGlobal->PABE = oldpabe;
	gsGlobal->PrimAlpha=oldalpha;
	gsKit_set_primalpha(gsGlobal, gsGlobal->PrimAlpha, gsGlobal->PABE);

	if(xRel != NULL)
		*xRel = x - StartX;
	if(yRel != NULL)
		*yRel = y - StartY;
}

void FontPrintf(GSGLOBAL *gsGlobal, short int x, short int y, short int z, float scale, u64 colour, const char *string)
{
	return FontPrintfWithFeedback(gsGlobal, x, y, z, scale, colour, string, NULL, NULL);
}

static int GetGlyphWidth(GSGLOBAL *gsGlobal, Font_t *font, wint_t character, int DrawMissingGlyph)
{
	struct FontGlyphSlotInfo glyphInfo;
	int result;

	if(font->IsLoaded)
	{	//Calling FT_Get_Advance is slow when I/O is slow, hence a cache is required. Here, the atlas is used as a cache.
		if ((result = GetGlyph(gsGlobal, font, character, DrawMissingGlyph, &glyphInfo)) != 0)
			return result;

		return glyphInfo.slot->advance_x;
	}

	return 0;
}

int FontGetGlyphWidth(GSGLOBAL *gsGlobal, wint_t character)
{
	int width;

	if((width = GetGlyphWidth(gsGlobal, &GS_FTFont, character, 0)) == 0)
	{
		if((width = GetGlyphWidth(gsGlobal, &GS_sub_FTFont, character, 0)) == 0)
		{
			width = GetGlyphWidth(gsGlobal, &GS_FTFont, character, 1);
		}
	}

	return width;
}

void wFontPrintfWithFeedback(GSGLOBAL *gsGlobal, short x, short int y, short int z, float scale, u64 colour, const wchar_t *string, short int *xRel, short int *yRel)
{
	wchar_t wchar;
	short int StartX, StartY, width;

	u64 oldalpha = gsGlobal->PrimAlpha;
	unsigned char oldpabe = gsGlobal->PABE;
	gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0,1,0,1,0), GS_SETTING_OFF);

	StartX = x;
	StartY = y;

	for(; *string != '\0'; string++)
	{
		wchar = *string;

		switch(wchar)
		{
			case '\r':
				x=StartX;
				break;
			case '\n':
				y+=(short int)(FNT_CHAR_HEIGHT*scale);
				x=StartX;
				break;
			case '\t':
				x+=(short int)(FNT_TAB_STOPS*FNT_CHAR_WIDTH*scale)-(unsigned int)x%(unsigned int)(FNT_TAB_STOPS*FNT_CHAR_WIDTH*scale);
				break;
			default:
				width = 0;
				if(DrawGlyph(gsGlobal, &GS_FTFont, wchar, x, y, z, scale, colour, 0, &width) != 0)
				{
					if(DrawGlyph(gsGlobal, &GS_sub_FTFont, wchar, x, y, z, scale, colour, 0, &width) != 0)
					{	//Cannot locate the glyph, so draw the missing glyph character.
						DrawGlyph(gsGlobal, &GS_FTFont, wchar, x, y, z, scale, colour, 1, &width);
					}
				}

				x += width;
		}
	}

	gsGlobal->PABE = oldpabe;
	gsGlobal->PrimAlpha=oldalpha;
	gsKit_set_primalpha(gsGlobal, gsGlobal->PrimAlpha, gsGlobal->PABE);

	if(xRel != NULL)
		*xRel = x - StartX;
	if(yRel != NULL)
		*yRel = y - StartY;
}

void wFontPrintf(GSGLOBAL *gsGlobal, short int x, short int y, short int z, float scale, u64 colour, const wchar_t *string)
{
	return wFontPrintfWithFeedback(gsGlobal, x, y, z, scale, colour, string, NULL, NULL);
}

int wFontPrintField(GSGLOBAL *gsGlobal, short int x, short int y, short int z, float scale, u64 colour, const wchar_t *string, unsigned int spacePix, int cursor)
{
	unsigned int totalWidth;
	wchar_t wchar;
	short int StartX, StartY, i, width;

	u64 oldalpha = gsGlobal->PrimAlpha;
	unsigned char oldpabe = gsGlobal->PABE;
	gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0,1,0,1,0), GS_SETTING_OFF);

	StartX = x;
	StartY = y;

	for(i = 0, totalWidth = 0; *string != '\0' && totalWidth < spacePix; string++,i++)
	{
		if(cursor >= 0)
		{	//If a cursor is to be drawn, draw one at the specified position.
			if(i == cursor)
				gsKit_prim_line(gsGlobal, x, y, x, y+(short int)(FNT_CHAR_HEIGHT*scale), 0, colour);
		}

		wchar = *string;

		width = 0;
		if(DrawGlyph(gsGlobal, &GS_FTFont, wchar, x, y, z, scale, colour, 0, &width) != 0)
		{
			if(DrawGlyph(gsGlobal, &GS_sub_FTFont, wchar, x, y, z, scale, colour, 0, &width) != 0)
			{	//Cannot locate the glyph, so draw the missing glyph character.
				DrawGlyph(gsGlobal, &GS_FTFont, wchar, x, y, z, scale, colour, 1, &width);
			}
		}

		x += width;
		totalWidth += width;
	}

	if(cursor == i)
	{	//If a cursor is to be drawn, draw one at the specified position.
		gsKit_prim_line(gsGlobal, x, y, x, y+(short int)(FNT_CHAR_HEIGHT*scale), 0, colour);
	}

	gsGlobal->PABE = oldpabe;
	gsGlobal->PrimAlpha=oldalpha;
	gsKit_set_primalpha(gsGlobal, gsGlobal->PrimAlpha, gsGlobal->PABE);

	return i;
}

int wFontPrintTitle(GSGLOBAL *gsGlobal, short int x, short int y, short int z, float scale, u64 colour, const wchar_t *string, unsigned int spacePix)
{
	unsigned int totalWidth;
	wchar_t wchar;
	short int StartX, StartY, i, width, j;

	u64 oldalpha = gsGlobal->PrimAlpha;
	unsigned char oldpabe = gsGlobal->PABE;
	gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0,1,0,1,0), GS_SETTING_OFF);

	StartX = x;
	StartY = y;

	for(i = 0, totalWidth = 0; *string != '\0'; string++,i++)
	{
		wchar = *string;

		if(totalWidth >= spacePix - 3*FNT_CHAR_WIDTH)
		{	//Count the number of characters left. Break if it is too long.
			if(wcslen(string) > 3)
				break;
		}

		width = 0;
		if(DrawGlyph(gsGlobal, &GS_FTFont, wchar, x, y, z, scale, colour, 0, &width) != 0)
		{
			if(DrawGlyph(gsGlobal, &GS_sub_FTFont, wchar, x, y, z, scale, colour, 0, &width) != 0)
			{	//Cannot locate the glyph, so draw the missing glyph character.
				DrawGlyph(gsGlobal, &GS_FTFont, wchar, x, y, z, scale, colour, 1, &width);
			}
		}

		x += width;
		totalWidth += width;
	}

	if(*string != '\0')
	{	//We're done, but there are still characters left! The message must be truncated.
		for(i = 0; i < 3; i++)
		{
			width = 0;
			DrawGlyph(gsGlobal, &GS_FTFont, '.', x, y, z, scale, colour, 0, &width);
			x += width;
		}
	}

	gsGlobal->PABE = oldpabe;
	gsGlobal->PrimAlpha=oldalpha;
	gsKit_set_primalpha(gsGlobal, gsGlobal->PrimAlpha, gsGlobal->PABE);

	return i;
}
