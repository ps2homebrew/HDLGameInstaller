#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>

#include <kernel.h>
#include <sifrpc.h>

#include <math3d.h>

#include <gsKit.h>
#include <dmaKit.h>
#include <gsToolkit.h>

#include "IconLoader.h"
#include "IconRender.h"

extern GSFONTM *gsFontM;
extern GSGLOBAL *gsGlobal;

void ResetIconModelRuntimeData(const struct PS2IconModel *IconModel, struct IconModelAnimRuntimeData *IconRuntimeData){
	unsigned int i, shape;

	for(i=0; i<IconModel->ModelSectionHeader.nbvtx; i++){
		for(shape=0; shape<IconModel->ModelSectionHeader.nbsp; shape++){
			memcpy(IconRuntimeData->vertices[i], IconModel->ModelVertices[i].vtx, sizeof(struct Vector)*IconModel->ModelSectionHeader.nbsp);
		}
	}
}

int InitIconModelRuntimeData(const struct PS2IconModel *IconModel, struct IconModelAnimRuntimeData *IconRuntimeData){
	unsigned int i;

	memset(IconRuntimeData, 0, sizeof(struct IconModelAnimRuntimeData));

	IconRuntimeData->vertices=malloc(sizeof(struct Vector*)*IconModel->ModelSectionHeader.nbvtx);
	for(i=0; i<IconModel->ModelSectionHeader.nbvtx; i++){
		IconRuntimeData->vertices[i]=malloc(sizeof(struct Vector)*IconModel->ModelSectionHeader.nbsp);
	}

	ResetIconModelRuntimeData(IconModel, IconRuntimeData);

	return 0;
}

void FreeIconModelRuntimeData(const struct PS2IconModel *IconModel, struct IconModelAnimRuntimeData *IconRuntimeData){
	unsigned int i;

	for(i=0; i<IconModel->ModelSectionHeader.nbvtx; i++){
		if(IconRuntimeData->vertices[i]!=NULL) free(IconRuntimeData->vertices[i]);
	}

	if(IconRuntimeData->vertices!=NULL) free(IconRuntimeData->vertices);
}

int UploadIcon(const struct PS2IconModel *IconModel, GSTEXTURE *texture){
	texture->Width=128;
	texture->Height=128;
	texture->Filter=GS_FILTER_LINEAR;
	texture->PSM=GS_PSM_CT16;
	texture->Clut=NULL;
	texture->VramClut=0;
	texture->Delayed=GS_SETTING_ON;

	gsKit_setup_tbw(texture);
	gsKit_texture_send_inline(gsGlobal, IconModel->texture, texture->Width, texture->Height, texture->Vram, texture->PSM, texture->TBW, texture->VramClut);

	return 0;
}

void TransformIcon(unsigned int frame, float x, float y, int z, float scale, const struct PS2IconModel *IconModel, const struct IconModelAnimRuntimeData *IconRuntimeData){
	unsigned int shape, vertex;
	VECTOR ResultVector, SourceVector, ScaleVector, TranslateVector;//, RotationVector;
	MATRIX TransformMatrix;

	matrix_unit(TransformMatrix);

	//TODO: How to handle the animation data? D:

	//Handle scaling.
	ScaleVector[0]=scale;
	ScaleVector[1]=scale;
	ScaleVector[2]=scale;
	matrix_scale(TransformMatrix, TransformMatrix, ScaleVector);

	//Handle translation.
	TranslateVector[0]=x;
	TranslateVector[1]=y;
	TranslateVector[2]=z;
	matrix_translate(TransformMatrix, TransformMatrix, TranslateVector);

	//Handle rotation
/*	RotationVector[0]=(frame%3600)/3600.0f;
	RotationVector[1]=0;
	RotationVector[2]=-(frame%3600)/3600.0f;
	matrix_rotate(TransformMatrix, TransformMatrix, RotationVector);
	matrix_multiply(TransformMatrix, TransformMatrix, LocalWorldMatrix); */

	//Transform!
	for(vertex=0; vertex<IconModel->ModelSectionHeader.nbvtx; vertex++){
		for(shape=0; shape<IconModel->ModelSectionHeader.nbsp; shape++){
			SourceVector[0]=IconModel->ModelVertices[vertex].vtx[shape].x;
			SourceVector[1]=IconModel->ModelVertices[vertex].vtx[shape].y;
			SourceVector[2]=IconModel->ModelVertices[vertex].vtx[shape].z;
			vector_apply(ResultVector, SourceVector, TransformMatrix);
			IconRuntimeData->vertices[vertex][shape].x=ResultVector[0];
			IconRuntimeData->vertices[vertex][shape].y=ResultVector[1];
			IconRuntimeData->vertices[vertex][shape].z=ResultVector[2];
		}
	}
}

void DrawIcon(const struct PS2IconModel *IconModel, const struct IconModelAnimRuntimeData *IconRuntimeData, GSTEXTURE *IconModelTexture){
	unsigned int i, shape;
	struct IconModelVertex *ModelVertices;
	struct Vector **RuntimeVertexData;
	shape=0;

	if(IconModel->ModelVertices!=NULL){
		for(i=0,ModelVertices=IconModel->ModelVertices,RuntimeVertexData=IconRuntimeData->vertices; i<IconModel->ModelSectionHeader.nbvtx; i+=3,ModelVertices+=3,RuntimeVertexData+=3){
			gsKit_prim_triangle_texture_3d(gsGlobal, IconModelTexture,
							RuntimeVertexData[0][shape].x, RuntimeVertexData[0][shape].y, 0, ModelVertices[0].st[0]*IconModelTexture->Width, ModelVertices[0].st[1]*IconModelTexture->Height,
							RuntimeVertexData[1][shape].x, RuntimeVertexData[1][shape].y, 0, ModelVertices[1].st[0]*IconModelTexture->Width, ModelVertices[1].st[1]*IconModelTexture->Height,
							RuntimeVertexData[2][shape].x, RuntimeVertexData[2][shape].y, 0, ModelVertices[2].st[0]*IconModelTexture->Width, ModelVertices[2].st[1]*IconModelTexture->Height, GS_SETREG_RGBAQ(0x80,0x80,0x80,0x80,0x00));
		}
	}
}
