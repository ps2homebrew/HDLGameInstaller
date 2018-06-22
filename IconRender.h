struct IconModelAnimRuntimeData{
	struct Vector **vertices;
};

void ResetIconModelRuntimeData(const struct PS2IconModel *IconModel, struct IconModelAnimRuntimeData *IconRuntimeData);
int InitIconModelRuntimeData(const struct PS2IconModel *IconModel, struct IconModelAnimRuntimeData *IconRuntimeData);
void FreeIconModelRuntimeData(const struct PS2IconModel *IconModel, struct IconModelAnimRuntimeData *IconRuntimeData);
int UploadIcon(const struct PS2IconModel *IconModel, GSTEXTURE *texture);
void TransformIcon(unsigned int frame, float x, float y, int z, float scale, const struct PS2IconModel *IconModel, const struct IconModelAnimRuntimeData *IconRuntimeData);
void DrawIcon(const struct PS2IconModel *IconModel, const struct IconModelAnimRuntimeData *IconRuntimeData, GSTEXTURE *IconModelTexture);
