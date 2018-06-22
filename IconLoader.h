//For icon file saving/loading:

#define ICON_FILE_VERSION	0x10000

struct IconFileVersionHeader{
	unsigned int version;
};

#define ICON_MODEL_ATTR_IIP		0x01	//Shading method. 0 = flat, 1 = gouraud.
#define ICON_MODEL_ATTR_ANTI	0x02	//Anti-aliasing on/off.
#define ICON_MODEL_ATTR_TEX		0x04	//Texture pasting on/off.
#define ICON_MODEL_ATTR_RLE		0x08	//Texture compression flag. 0 = Uncompressed, 1 = compressed.

struct IconFileModelSectionHeader{
	unsigned int nbsp;		//Number of key shapes (1, 4, 6 or 8)
	unsigned int attrib;	//Model attributes
	float bface;			//Back-face clipping standard value (Normally 1.0).
	unsigned int nbvtx;		//Number of vertices (Must be a multiple of 3).
};

//The following structure is then repeated nbtx times.
/* struct IconFileModelVertex{
	unsigned short int vtx[4*nbsp];	//3x6:10 fixed-point elements for each point of every coordinate, with 2-byte padding.
	unsigned short int normal[4];	//3x4:12 fixed-point elements for each point, with 2-byte padding.
	unsigned short int st[2];		//Texture coordinates: 2x4:12 fixed-point elements.
	unsigned char color[4];			//Vertex color:	1 byte for R, G, B and A. A is ignored.
}; */

struct IconFileModelAnimSectionHeader{
	unsigned int nbseq;	//Number of sequences (Only 1 is currently supported).
};

//The following structure is repeated nbseq times.
struct IconFileModelAnimSequence{
	unsigned int nbframe;	//Frame length.
	float speed;			//Play speed magnification.
	unsigned int offset;	//Play offset (frame number).
	unsigned int nbksp;		//Number of shapes to be used (maximum 8).
};

struct IconFileModelAnimFrameKeyData{
	float frame;	//Frame number of key frame.
	float weight;	//Shape weight.
};

//The following structure is repeated nbksp times
struct IconFileModelAnimFrame{
	unsigned int kspid;									//Shape number.
	unsigned int nbkf;									//Number of key frames (maximum 10).
//	struct IconFileModelAnimFrameKeyData keys[];		//Key data goes here in the file.
};

/* Texture data begins here. The content depends on whether it's compressed or not.
If it's compressed:
	unsigned int size -> size of compressed data.
	<data>	-> RLE-encoded 128x128 PSMCT16 image, compressed in 2-byte units.

If is's uncompressed:
	<data>	-> Uncompressed, raw 128x128 PSMCT16 image.
*/

/**************************/
//For icon storage in RAM.
struct Vector{
	float x, y, z;
};

struct IconModelVertex{
	struct Vector *vtx;	//Repeated nbsp times
	float normal[3];
	float st[2];
	unsigned char color[4];
};

struct IconModelAnimFrame{
	unsigned int kspid;									//Shape number.
	unsigned int nbkf;									//Number of key frames (maximum 10).
	struct IconFileModelAnimFrameKeyData *keys;		//Key data.
};

struct IconModelAnimSequence{
	unsigned int nbframe;	//Frame length.
	float speed;			//Play speed magnification.
	unsigned int offset;	//Play offset (frame number).
	unsigned int nbksp;		//Number of shapes to be used (maximum 8).
	struct IconModelAnimFrame *frames;
};

struct PS2IconModel{
	struct IconFileModelSectionHeader ModelSectionHeader;
	struct IconModelVertex *ModelVertices;
	struct IconFileModelAnimSectionHeader ModelAnimSectionHeader;
	struct IconModelAnimSequence *ModelAnimSequences;
	void *texture;
};

/**************************/
//Function prototypes.
int LoadPS2IconModel(const char *path, struct PS2IconModel *IconModel);
void UnloadPS2IconModel(struct PS2IconModel *IconModel);
