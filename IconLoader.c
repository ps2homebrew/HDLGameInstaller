#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "IconLoader.h"

#define ITOF10(x) ((float)(x)/1024.0f)
#define ITOF12(x) ((float)(x)/4096.0f)
#define FTOI10(x) ((short)((x)*1024.0f))
#define FTOI12(x) ((short)((x)*4096.0f))

static inline int DecompressRLEData(void *output, unsigned int OutputBufferLength, const void *PackedData){
	const void *PackedDataPayload;
	unsigned int PackedDataLength, offset;
	short int *pInput, BlockLength;
	unsigned short int *pOutput, i;

	PackedDataLength=*(unsigned int*)PackedData;
	PackedDataPayload=(void*)((unsigned int)PackedData+4);
	for(offset=0,pInput=(short int*)PackedDataPayload,pOutput=(unsigned short int*)output; offset<PackedDataLength && offset<OutputBufferLength;){
		BlockLength=*pInput++;
		offset+=2;
		if(BlockLength>0){
			for(i=0; i<BlockLength; i++,pOutput++) *pOutput=*pInput;
			pInput++;
			offset+=sizeof(unsigned short);
		}
		else{
			BlockLength=-BlockLength;
			memcpy(pOutput, pInput, BlockLength*sizeof(unsigned short));
			pOutput+=BlockLength;
			pInput+=BlockLength;
			offset+=BlockLength*sizeof(unsigned short);
		}
	}

	return 0;
}

int LoadPS2IconModel(const char *path, struct PS2IconModel *IconModel){
	int result, size;
	unsigned int vertex, i, sequence, TextureLength;
	FILE *IconFile;
	void *buffer;
	struct IconFileVersionHeader *VersionHeader;
	struct IconFileModelSectionHeader *ModelSectionHeader;
	struct Vector *PointVector;
	void *IconFileDataOffset;
	struct IconFileModelAnimSectionHeader *AnimSectionHeader;
	struct IconFileModelAnimSequence *AnimSequence;
	struct IconFileModelAnimFrame *AnimeFrame;

	memset(IconModel, 0, sizeof(struct PS2IconModel));
	result=0;

	if((IconFile=fopen(path, "rb"))!=NULL){
		fseek(IconFile, 0, SEEK_END);
		size=ftell(IconFile);
		rewind(IconFile);
		if((buffer=malloc(size))!=NULL){
			result=fread(buffer, 1, size, IconFile);
		}

		fclose(IconFile);

		if(buffer!=NULL){
			if(result==size){
				VersionHeader=(struct IconFileVersionHeader*)buffer;
				if(VersionHeader->version==ICON_FILE_VERSION){
					ModelSectionHeader=(struct IconFileModelSectionHeader*)((unsigned int)buffer+sizeof(struct IconFileVersionHeader));

					//printf("LoadIconModel: attrib: 0x%08x, nbvtx: %u, nbsp: %u\n", ModelSectionHeader->attrib, ModelSectionHeader->nbvtx, ModelSectionHeader->nbsp);

					memcpy(&IconModel->ModelSectionHeader, ModelSectionHeader, sizeof(struct IconFileModelSectionHeader));
					IconModel->ModelVertices=(struct IconModelVertex*)malloc(ModelSectionHeader->nbvtx*sizeof(struct IconModelVertex));
					IconFileDataOffset=(void*)((unsigned int)ModelSectionHeader+sizeof(struct IconFileModelSectionHeader));
					for(vertex=0; vertex<ModelSectionHeader->nbvtx; vertex++){
						//Start with the points of the vertex.
						IconModel->ModelVertices[vertex].vtx=malloc(ModelSectionHeader->nbsp*sizeof(struct Vector));
						for(i=0; i<ModelSectionHeader->nbsp; i++){
							PointVector=&IconModel->ModelVertices[vertex].vtx[i];

							PointVector->x=ITOF10(((unsigned short int*)IconFileDataOffset)[0]);
							PointVector->y=ITOF10(((unsigned short int*)IconFileDataOffset)[1]);
							PointVector->z=ITOF10(((unsigned short int*)IconFileDataOffset)[2]);
							IconFileDataOffset=(void*)((unsigned int)IconFileDataOffset+8);
						}

						IconModel->ModelVertices[vertex].normal[0]=ITOF12(((unsigned short int*)IconFileDataOffset)[0]);
						IconModel->ModelVertices[vertex].normal[1]=ITOF12(((unsigned short int*)IconFileDataOffset)[1]);
						IconModel->ModelVertices[vertex].normal[2]=ITOF12(((unsigned short int*)IconFileDataOffset)[2]);
						IconFileDataOffset=(void*)((unsigned int)IconFileDataOffset+8);

						IconModel->ModelVertices[vertex].st[0]=ITOF12(((unsigned short int*)IconFileDataOffset)[0]);
						IconModel->ModelVertices[vertex].st[1]=ITOF12(((unsigned short int*)IconFileDataOffset)[1]);
						IconFileDataOffset=(void*)((unsigned int)IconFileDataOffset+4);

						memcpy(IconModel->ModelVertices[vertex].color, IconFileDataOffset, 4);
						IconFileDataOffset=(void*)((unsigned int)IconFileDataOffset+4);
					}

					AnimSectionHeader=(struct IconFileModelAnimSectionHeader*)IconFileDataOffset;
					IconFileDataOffset=(void*)((unsigned int)IconFileDataOffset+sizeof(struct IconFileModelAnimSectionHeader));
					IconModel->ModelAnimSequences=(struct IconModelAnimSequence*)malloc(AnimSectionHeader->nbseq*sizeof(struct IconModelAnimSequence));
					memcpy(&IconModel->ModelAnimSectionHeader, AnimSectionHeader, sizeof(struct IconFileModelAnimSectionHeader));

					//printf("AnimSectionHeader->nbseq: %d\n", AnimSectionHeader->nbseq);
					for(sequence=0; sequence<AnimSectionHeader->nbseq; sequence++){
						AnimSequence=(struct IconFileModelAnimSequence*)IconFileDataOffset;
						IconModel->ModelAnimSequences[sequence].nbframe=AnimSequence->nbframe;
						IconModel->ModelAnimSequences[sequence].speed=AnimSequence->speed;
						IconModel->ModelAnimSequences[sequence].offset=AnimSequence->offset;
						IconModel->ModelAnimSequences[sequence].nbksp=AnimSequence->nbksp;
						IconModel->ModelAnimSequences[sequence].frames=(struct IconModelAnimFrame*)malloc(AnimSequence->nbksp*sizeof(struct IconModelAnimFrame));

						//printf("AnimSequence->nbksp: %d\n", AnimSequence->nbksp);

						IconFileDataOffset=(void*)((unsigned int)IconFileDataOffset+sizeof(struct IconFileModelAnimSequence));
						for(i=0; i<AnimSequence->nbksp; i++){
							AnimeFrame=(struct IconFileModelAnimFrame*)IconFileDataOffset;

							IconModel->ModelAnimSequences[sequence].frames[i].kspid=AnimeFrame->kspid;
							IconModel->ModelAnimSequences[sequence].frames[i].nbkf=AnimeFrame->nbkf;
							IconModel->ModelAnimSequences[sequence].frames[i].keys=(struct IconFileModelAnimFrameKeyData*)malloc(AnimSequence->nbksp*sizeof(struct IconFileModelAnimFrameKeyData));
							memcpy(IconModel->ModelAnimSequences[sequence].frames[i].keys, (void*)((unsigned int)AnimeFrame+sizeof(struct IconFileModelAnimFrame)), AnimSequence->nbksp*sizeof(struct IconFileModelAnimFrameKeyData));

							IconFileDataOffset=(void*)((unsigned int)IconFileDataOffset+8+sizeof(struct IconFileModelAnimFrameKeyData)*AnimSequence->nbksp);
						}
					}

					//Texture loading.
					TextureLength=size-((unsigned int)IconFileDataOffset-(unsigned int)buffer);
					if(ModelSectionHeader->attrib&ICON_MODEL_ATTR_RLE){
						IconModel->texture=memalign(64, 32768);//2*TextureLength);
						result=DecompressRLEData(IconModel->texture, 32768, IconFileDataOffset);
					}
					else{
						//if(TextureLength==32768){
							if(TextureLength>32768) TextureLength=32768;
							IconModel->texture=memalign(64, 32768);	//Should be 128x128x16-bit=32768
							memcpy(IconModel->texture, IconFileDataOffset, TextureLength);
							result=0;
						/* }
						else{
							printf("TextureLength: %d\n", TextureLength);
							result=-1;
						} */
					}
				}
				else result=-1;	//Unsupported version.
			}
			else result=EIO;

			free(buffer);
		}
	}
	else result=ENOENT;

	return result;
}

void UnloadPS2IconModel(struct PS2IconModel *IconModel){
	unsigned int i, vertex, sequence;

	for(vertex=0; vertex<IconModel->ModelSectionHeader.nbvtx; vertex++){
		if(IconModel->ModelVertices[vertex].vtx!=NULL) free(IconModel->ModelVertices[vertex].vtx);
	}
	if(IconModel->ModelVertices!=NULL) free(IconModel->ModelVertices);
	for(sequence=0; sequence<IconModel->ModelAnimSectionHeader.nbseq; sequence++){
		for(i=0; i<IconModel->ModelAnimSequences[sequence].nbksp; i++){
			if(IconModel->ModelAnimSequences[sequence].frames[i].keys!=NULL) free(IconModel->ModelAnimSequences[sequence].frames[i].keys);
		}
		if(IconModel->ModelAnimSequences[sequence].frames!=NULL) free(IconModel->ModelAnimSequences[sequence].frames);
	}
	if(IconModel->ModelAnimSequences!=NULL) free(IconModel->ModelAnimSequences);
	if(IconModel->texture!=NULL) free(IconModel->texture);
}
