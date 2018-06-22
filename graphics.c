#include <kernel.h>
#include <libcdvd.h>
#include <libpad.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <gsKit.h>
#include <dmaKit.h>
#include <gsToolkit.h>

#include <png.h>

#include "main.h"
#include "HDLGameList.h"
#include "graphics.h"
#include "pad.h"

#include "font.h"

extern GSGLOBAL *gsGlobal;

extern int VBlankStartSema;

int UploadTexture(GSGLOBAL *gsGlobal, GSTEXTURE* Texture){
	u8 Clut;
	int ClutWidth, ClutHeight;
	u32 VramTextureSize = gsKit_texture_size(Texture->Width, Texture->Height, Texture->PSM);

	if((Texture->Vram = gsKit_vram_alloc(gsGlobal, VramTextureSize, GSKIT_ALLOC_USERBUFFER)) == GSKIT_ALLOC_ERROR)
	{
		printf("VRAM allocation failed. Will not upload texture.\n");
		return -ENOMEM;
	}

	if (Texture->PSM == GS_PSM_T8 || Texture->PSM == GS_PSM_T4)
	{
		if(Texture->PSM==GS_PSM_T8){
			ClutWidth=ClutHeight=16;
		}
		else{
			ClutWidth=8;
			ClutHeight=2;
		}

		Texture->VramClut = gsKit_vram_alloc(gsGlobal, gsKit_texture_size(ClutWidth, ClutHeight, Texture->ClutPSM), GSKIT_ALLOC_USERBUFFER);
		Clut=GS_CLUT_TEXTURE;
	}
	else{
		ClutWidth=ClutHeight=0;
		Clut=GS_CLUT_NONE;
	}

	gsKit_texture_send_inline(gsGlobal, Texture->Mem, Texture->Width, Texture->Height, Texture->Vram, Texture->PSM, Texture->TBW, Clut);
	if(Clut!=GS_CLUT_NONE) gsKit_texture_send_inline(gsGlobal, Texture->Clut, ClutWidth, ClutHeight, Texture->VramClut, Texture->ClutPSM, 1, GS_CLUT_PALLETE);

	return 0;
}

static void PNGReadMem(png_structp pngPtr, png_bytep data, png_size_t length){
	u8 **PngBufferPtr=(u8**)png_get_io_ptr(pngPtr);

	memcpy(data, *PngBufferPtr, length);
	*PngBufferPtr += length;
}

static int LoadPNGImage(GSGLOBAL *gsGlobal, GSTEXTURE *Texture, const void* buffer, unsigned int size){
	void **PngFileBufferPtr;
	png_structp png_ptr;
	png_infop info_ptr;
	png_uint_32 width, height;
	png_bytep *row_pointers;

	unsigned int sig_read = 0;
        int row, i, k=0, j, bit_depth, color_type, interlace_type;

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, (png_voidp) NULL, NULL, NULL);

	if(!png_ptr)
	{
		//printf("PNG Read Struct Init Failed\n");
		return -1;
	}

	info_ptr = png_create_info_struct(png_ptr);

	if(!info_ptr)
	{
		//printf("PNG Info Struct Init Failed\n");
		png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
		return -1;
	}

	if(setjmp(png_jmpbuf(png_ptr)))
	{
		//printf("Got PNG Error!\n");
		png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
		return -1;
	}

	PngFileBufferPtr=(void*)buffer;
	png_set_read_fn(png_ptr, &PngFileBufferPtr, &PNGReadMem);
	png_set_sig_bytes(png_ptr, sig_read);
	png_read_info(png_ptr, info_ptr);
	png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, NULL, NULL);

	png_set_strip_16(png_ptr);

	if (color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_expand(png_ptr);

	if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
		png_set_expand(png_ptr);

	if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png_ptr);

	png_set_filler(png_ptr, 0xff, PNG_FILLER_AFTER);

	png_read_update_info(png_ptr, info_ptr);

	Texture->Width = width;
	Texture->Height = height;

        Texture->VramClut = 0;
        Texture->Clut = NULL;

	if(png_get_color_type(png_ptr, info_ptr) == PNG_COLOR_TYPE_RGB_ALPHA)
	{
		int row_bytes = png_get_rowbytes(png_ptr, info_ptr);
		Texture->PSM = GS_PSM_CT32;
		Texture->Mem = memalign(128, gsKit_texture_size_ee(Texture->Width, Texture->Height, Texture->PSM));

		row_pointers = calloc(height, sizeof(png_bytep));

		for (row = 0; row < height; row++) row_pointers[row] = malloc(row_bytes);

		png_read_image(png_ptr, row_pointers);

		struct pixel { unsigned char r,g,b,a; };
		struct pixel *Pixels = (struct pixel *) Texture->Mem;

		for (i=0;i<height;i++) {
			for (j=0;j<width;j++) {
				Pixels[k].r = row_pointers[i][4*j];
				Pixels[k].g = row_pointers[i][4*j+1];
				Pixels[k].b = row_pointers[i][4*j+2];
				Pixels[k++].a = 128-((int) row_pointers[i][4*j+3] * 128 / 255);
			}
		}

		for(row = 0; row < height; row++) free(row_pointers[row]);

		free(row_pointers);
	}
	else if(png_get_color_type(png_ptr, info_ptr) == PNG_COLOR_TYPE_RGB)
	{
		int row_bytes = png_get_rowbytes(png_ptr, info_ptr);
		Texture->PSM = GS_PSM_CT24;
		Texture->Mem = memalign(128, gsKit_texture_size_ee(Texture->Width, Texture->Height, Texture->PSM));

		row_pointers = calloc(height, sizeof(png_bytep));

		for(row = 0; row < height; row++) row_pointers[row] = malloc(row_bytes);

		png_read_image(png_ptr, row_pointers);

		struct pixel3 { unsigned char r,g,b; };
		struct pixel3 *Pixels = (struct pixel3 *) Texture->Mem;

		for (i=0;i<height;i++) {
			for (j=0;j<width;j++) {
				Pixels[k].r = row_pointers[i][4*j];
				Pixels[k].g = row_pointers[i][4*j+1];
				Pixels[k++].b = row_pointers[i][4*j+2];
			}
		}

		for(row = 0; row < height; row++) free(row_pointers[row]);

		free(row_pointers);
	}
	else
	{
		//printf("This texture depth is not supported yet!\n");
		return -1;
	}

	Texture->Filter = GS_FILTER_NEAREST;
	png_read_end(png_ptr, NULL);
	png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp) NULL);

	if(!Texture->Delayed)
	{
		u32 VramTextureSize = gsKit_texture_size(Texture->Width, Texture->Height, Texture->PSM);
		Texture->Vram = gsKit_vram_alloc(gsGlobal, VramTextureSize, GSKIT_ALLOC_USERBUFFER);
		if(Texture->Vram == GSKIT_ALLOC_ERROR)
		{
			printf("VRAM Allocation Failed. Will not upload texture.\n");
			return -1;
		}

		gsKit_texture_upload(gsGlobal, Texture);
		free(Texture->Mem);
	}
	else
	{
		gsKit_setup_tbw(Texture);
	}

	return 0;
}

extern unsigned char buttons[];
extern unsigned int size_buttons;

extern unsigned char devices[];
extern unsigned int size_devices;

extern unsigned char background[];
extern unsigned int size_background;

int LoadBackground(GSGLOBAL *gsGlobal, GSTEXTURE* Texture){
	return LoadPNGImage(gsGlobal, Texture, background, size_background);
}

int LoadPadGraphics(GSGLOBAL *gsGlobal, GSTEXTURE* Texture){
	return LoadPNGImage(gsGlobal, Texture, buttons, size_buttons);
}

int LoadDeviceIcons(GSGLOBAL *gsGlobal, GSTEXTURE* Texture){
	return LoadPNGImage(gsGlobal, Texture, devices, size_devices);
}

struct IconLayout{
	unsigned int u, v;
	unsigned int length, width;
};

static const struct IconLayout ButtonLayoutParameters[BUTTON_TYPE_COUNT]=
{
	{22, 0, 22, 22},	//Circle
	{0, 0, 22, 22},		//Cross
	{44, 0, 22, 22},	//Square
	{66, 0, 22, 22},	//Triangle
	{0, 22, 28, 20},	//L1
	{56, 22, 28, 20},	//R1
	{28, 22, 28, 20},	//L2
	{84, 22, 28, 20},	//R2
	{150, 42, 30, 30},	//L3
	{150, 72, 30, 30},	//R3
	{140, 22, 29, 19},	//START
	{112, 22, 28, 19},	//SELECT
	{120, 72, 30, 30},	//RSTICK
	{0, 72, 30, 30},	//UP RSTICK
	{30, 72, 30, 30},	//DOWN RSTICK
	{60, 72, 30, 30},	//LEFT RSTICK
	{90, 72, 30, 30},	//RIGHT RSTICK
	{120, 42, 30, 30},	//LSTICK
	{0, 42, 30, 30},	//UP LSTICK
	{30, 42, 30, 30},	//DOWN LSTICK
	{60, 42, 30, 30},	//LEFT LSTICK
	{90, 42, 30, 30},	//RIGHT LSTICK
	{104, 102, 26, 26},	//DPAD
	{130, 102, 26, 26},	//LR-DPAD
	{156, 102, 26, 26},	//UD-DPAD
	{0, 102, 26, 26},	//UP DPAD
	{26, 102, 26, 26},	//DOWN DPAD
	{52, 102, 26, 26},	//LEFT DPAD
	{78, 102, 26, 26},	//RIGHT DPAD
};

void DrawButtonLegendWithFeedback(GSGLOBAL *gsGlobal, GSTEXTURE* PadGraphicsTexture, unsigned char ButtonType, short int x, short int y, short int z, short int *xRel)
{
	const struct IconLayout *graphic;

	graphic = &ButtonLayoutParameters[ButtonType];
	//To centre the texels, add 0.5 to the coordinates.
	gsKit_prim_sprite_texture(gsGlobal, PadGraphicsTexture,
					x, y,
					graphic->u + 0.5f, graphic->v + 0.5f,
					x+graphic->length, y+graphic->width,
					graphic->u+graphic->length + 0.5f, graphic->v+graphic->width + 0.5f,
					z, GS_SETREG_RGBAQ(0x80,0x80,0x80,0x80,0x00));

	if(xRel != NULL)
		*xRel = graphic->length;
}

void DrawButtonLegend(GSGLOBAL *gsGlobal, GSTEXTURE* PadGraphicsTexture, unsigned char ButtonType, short int x, short int y, short int z)
{
	DrawButtonLegendWithFeedback(gsGlobal, PadGraphicsTexture, ButtonType, x, y, z, NULL);
}

static const struct IconLayout DeviceIcons[DEVICE_TYPE_COUNT]=
{
	{0, 1, 19, 22},		//Folder
	{19, 1, 18, 22},	//File
	{37, 1, 21, 22},	//Disk
	{58, 0, 17, 23},	//USB Disk
	{75, 2, 20, 21},	//ROM
	{95, 2, 17, 21},	//SD Card
	{112, 1, 22, 22},	//Disc
};

void DrawDeviceIconWithFeedback(GSGLOBAL *gsGlobal, GSTEXTURE* IconTexture, unsigned char icon, short int x, short int y, short int z, short int *xRel)
{
	const struct IconLayout *graphic;

	graphic = &DeviceIcons[icon];
	//To centre the texels, add 0.5 to the coordinates.
	gsKit_prim_sprite_texture(gsGlobal, IconTexture,
					x, y,
					graphic->u + 0.5f, graphic->v + 0.5f,
					x+(short int)(graphic->length*DEVICE_ICON_SCALE), y+(short int)(graphic->width*DEVICE_ICON_SCALE),
					graphic->u+graphic->length + 0.5f, graphic->v+graphic->width + 0.5f,
					z, GS_SETREG_RGBAQ(0x80,0x80,0x80,0x80,0x00));

	if(xRel != NULL)
		*xRel = (short int)(graphic->length * DEVICE_ICON_SCALE);
}

void DrawDeviceIcon(GSGLOBAL *gsGlobal, GSTEXTURE* IconTexture, unsigned char icon, short int x, short int y, short int z)
{
	DrawDeviceIconWithFeedback(gsGlobal, IconTexture, icon, x, y, z, NULL);
}

void DrawProgressBar(GSGLOBAL *gsGlobal, float percentage, short int x, short int y, short int z, short int len, u64 colour)
{
	char CharBuffer[8];
	float ProgressBarFillEndX;

	/* Draw the progress bar. */
	gsKit_prim_quad(gsGlobal, x, y, x+len, y, x, y+20, x+len, y+20, 4, GS_LGREY)
	ProgressBarFillEndX=x + (len - 10) *percentage;
	/* FIXME: For some unknown reason, the progress bar fill is being offset by -10 pixels. */
	gsKit_prim_quad(gsGlobal, x+5, y+5+10, ProgressBarFillEndX, y+5+10, x+5, y-5+10, ProgressBarFillEndX, y-5+10, 4, colour);
	snprintf(CharBuffer, sizeof(CharBuffer)/sizeof(char), "%u%%", (unsigned int)(percentage*100));
	FontPrintf(gsGlobal, x+len/2, y, z - 1, 1.0f, GS_WHITE_FONT, CharBuffer);
}

void DrawBackground(GSGLOBAL *gsGlobal, GSTEXTURE *background)
{
#ifdef CENTRE_BACKGROUND
	short int x, y;

	x=(gsGlobal->Width-background->Width)/2;
	y=(gsGlobal->Height-background->Height)/2;

	gsKit_clear(gsGlobal, GS_BLACK);

	//To centre the texels, add 0.5 to the coordinates.
	gsKit_prim_sprite_texture(gsGlobal, background,
					x, y,
					0.5f, 0.5f,
					x+background->Width, y+background->Height,
					background->Width + 0.5f, background->Height + 0.5f,
					8, GS_SETREG_RGBAQ(0x80,0x80,0x80,0x00,0x00));
#else
	gsKit_prim_sprite_texture(gsGlobal, background,
					0, 0,
					0.5f, 0.5f,
					gsGlobal->Width, gsGlobal->Height,
					background->Width + 0.5f, background->Height + 0.5f,
					8, GS_SETREG_RGBAQ(0x80,0x80,0x80,0x00,0x00));
#endif
}

void SyncFlipFB(void){
	PollSema(VBlankStartSema);	//Clear the semaphore to zero if it isn't already at zero, so that WaitSema will wait until the next VBlank start event.
	WaitSema(VBlankStartSema);
	gsKit_switch_context(gsGlobal);
	gsKit_queue_exec(gsGlobal);
}
