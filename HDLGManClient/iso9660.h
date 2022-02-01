#pragma pack(push, 1)

/* ISO9660 filesystem directory record */
struct iso9660_dirRec{
        u8 lenDR;
        u8 lenExAttr;

        u32 extentLoc_LE;
        u32 extentLoc_BE;

        u32 dataLen_LE;
        u32 dataLen_BE;

        u8 recDateTime[7];

        u8 fileFlags;
        u8 fileUnitSz;
        u8 interleaveGapSz;

        u16 volumeSeqNum_LE;
        u16 volumeSeqNum_BE;

        u8 lenFI;
        u8 fileIdentifier; /* The actual length is specified by lenFI */

        /* There are more fields after this. */
        /* Padding field; Only present if the length of the file identifier is an even number. */
        /* Field for system use; up to lenDR */
};

/* ISO9660 filesystem file path table */
struct iso9660_pathtable{
        u8 lenDI;
        u8 lenExRec;

        u32 extLoc;

        u16 parentDirNum;

        u8 dirIdentifier; /* The actual length is specified by lenDI. */

        /* There are more fields after this. */
        /* Padding field; Only present if the length of the directory identifier is an even number. */
};

#pragma pack(pop)

int ParsePS2CNF(void *discimg, char *DiscID, char *StartupFilename, u8 *targetpath, u8 sectortype);
u32 GetFileLSN(const char *path, void *discimg, u8 sectortype, unsigned int *file_size);
int GetDiscInfo(u8 source, void *discImg, u32 *nSectorsLayer0, u32 *nSectorsLayer1, u8 *discType);
int ReadSectors(void *discImg, u8 sectortype, u32 lsn, unsigned int sectors, void *buffer);
