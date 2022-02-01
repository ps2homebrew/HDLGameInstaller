#include <limits.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "HDLGManClient.h"
#include "main.h"
#include "system.h"
#include "iso9660.h"

int ParsePS2CNF(void *discimg, char *DiscID, char *StartupFilename, unsigned char *targetpath, unsigned char sectortype)
{
    u32 cnflsn;
    char data[22], *cnf;
    unsigned char sectorBuff[2048];
    unsigned int cnf_size, BytesToRead, BytesRemaining;

    if (!(cnflsn = GetFileLSN("SYSTEM.CNF;1", discimg, sectortype, &cnf_size)))
        return 0;
#ifdef DEBUG_MODE
    printf("Done loading SYSTEM.CNF LSN.\n\
                Now parsing SYSTEM.CNF...\n");
#endif

    cnf = malloc(cnf_size + 1);
    memset(cnf, 0, cnf_size + 1);
    BytesRemaining = cnf_size;
    while (BytesRemaining > 0) {
        BytesToRead = (BytesRemaining > 2048) ? 2048 : BytesRemaining;

        if (ReadSectors(discimg, sectortype, cnflsn, 1, sectorBuff) != 1) {
            return 0;
        }

        cnflsn++;
        memcpy(&cnf[cnf_size - BytesRemaining], sectorBuff, BytesToRead);

        BytesRemaining -= BytesToRead;
    }

    if (strstr(cnf, "BOOT2") != NULL) {
#ifdef DEBUG_MODE
        printf("BOOT2 parameter found in SYSTEM.CNF.\n");
#endif
        strncpy(data, strstr(cnf, "cdrom0"), 22); /* Copy "cdrom0:\SLXX_XXX.XX;1" */
        data[21] = '\0';
        strncpy(StartupFilename, &data[8], 11);
        StartupFilename[11] = '\0';
    } else {
        free(cnf);
        return 0;
    }

    free(cnf);

#ifdef DEBUG_MODE
    printf("The operation completed sucessfully!\nELF pointed to by SYSTEM.CNF: %s\n", data);
#endif

    /* Generate the disc ID string in this format: SXXX-XXXXX. */
    strncpy(DiscID, StartupFilename, 4);
    DiscID[4] = '-';
    strncpy(&DiscID[5], &StartupFilename[5], 3);
    strncpy(&DiscID[8], &StartupFilename[9], 2);
    DiscID[10] = '\0';

#ifdef DEBUG_MODE
    printf("Disc ID: %s\n", DiscID);
#endif
    return 1;
}

static int isfilename(const char *path)
{
    register unsigned char ptr;

    for (ptr = 0; path[ptr] != '\\'; ptr++)
        if (path[ptr] == '.')
            return 1;
    return 0;
}

static void stripdirfilename(const char *path, char *target, unsigned int *extern_ptr)
{
    char *path_ptr;
    register unsigned int name_len;

    path_ptr = strchr(path, '\\');
    if (path_ptr == NULL) { /* A file's name */
        name_len = strlen(path);
        strcpy(target, path);
        if (extern_ptr != NULL)
            *extern_ptr += name_len;
    } else { /* A folder's name */
        name_len = (unsigned long)path_ptr - (unsigned long)path;
        strncpy(target, path, name_len);
        if (extern_ptr != NULL)
            *extern_ptr += name_len;
    }

#ifdef DEBUG_MODE
    printf("Stripped path: %s, External index: 0x0%x\n", target, *extern_ptr);
#endif
}

u32 GetFileLSN(const char *path, void *discimg, unsigned char sectortype, unsigned int *file_size)
{
    char tgtfname[14];
    unsigned int pathptr;
    unsigned char *databuf; /* Data buffer for disc information extraction */
    u32 extentLSN, lsn;
    unsigned char fileEntryName[16];

    struct iso9660_pathtable *ptable;
    struct iso9660_dirRec *dirRec;

    unsigned char IDlen, name_len;                                           /* The length of the file entry (On the ISO9660 disc image), and the length of the name of the file/directory that's being searched for. */
    u32 ptableSz, ptableLSN, rootDirRecLSN, recLSN, recGrp_offset, recGrpSz; /* Size of an entire directory record group. */

/* When debugging is not enabled, the specialised error handlers in this function are not created. */
#ifndef DEBUG_TTY_FEEDBACK
#define searchdirrecords_error srch_error_common
#define searchptable_error     srch_error_common
#endif

    databuf = malloc(2048);
    ptable  = (struct iso9660_pathtable *)databuf;

    /* Initilize all data fields */
    pathptr = 0;

    /* 1. Seek to the path table.
    2. begin tracing.
    3. Return LSN of file when found. */

    ReadSectors(discimg, sectortype, 16, 1, databuf); /* Read sector 16. */

    ptableLSN = *(unsigned long *)&databuf[140]; /* Read path table LSN. */
#ifdef DEBUG_MODE
    printf("LSN of type-L path table: 0x%x\n", ptableLSN);
#endif

    memcpy((unsigned char *)&ptableSz, &databuf[132], 4); /* Read the size of the type-L path table. */
#ifdef DEBUG_MODE
    printf("Size of the type-L path table: %d\n", ptableSz);
#endif

    rootDirRecLSN = *(unsigned long *)&databuf[158]; /* Read the root directory record LSN. */
#ifdef DEBUG_MODE
    printf("LSN of root directory record: 0x%x\n", rootDirRecLSN);
#endif

    while (path[pathptr] == '\\')
        pathptr++; /* Skip any '\' characters present */
    if (!isfilename(&path[pathptr]))
        goto searchptable; /* Continue tracing at path table if the file is not @ the root folder */

    extentLSN = rootDirRecLSN; /* Start in the root directory record */

    goto searchdirrecords;

searchptable:
    recLSN        = ptableLSN;
    recGrp_offset = 0;
    ReadSectors(discimg, sectortype, recLSN, 1, databuf); /* Read the path table. */

    stripdirfilename(&path[pathptr], tgtfname, &pathptr); /* "Strip and search..." */

    recGrpSz = ptableSz;
    name_len = strlen(tgtfname);
    DEBUG_PRINTF("Path table grp sz: 0x%lx\n", recGrpSz); /* Get the size of the entire path table group */
    do {
        if (recGrp_offset > recGrpSz) { /* Prevent an overflow when file is not found */
            free(databuf);
            return 0;
        }

        IDlen = ptable->lenDI; /* Get LEN_DI(Length of directory identifier) */
        DEBUG_PRINTF("Path table entry ID length: 0x%x\n", IDlen);

        memcpy((unsigned char *)&extentLSN, &ptable->extLoc, 4); /* Copy the location of the extent. */
        DEBUG_PRINTF("Extent LSN: 0x%lx\n", extentLSN);

#ifdef DEBUG_TTY_FEEDBACK
        memset(fileEntryName, 0, 16); /* 0-fill the file/folder name array */
#endif
        memcpy(fileEntryName, &ptable->dirIdentifier, IDlen); /* name + extension must be at least 1 character long + ";" (e.g. "n.bin;1") */
        DEBUG_PRINTF("Current directory name: %s\n\n", fileEntryName);

        if ((ptable->lenDI % 2) != 0)
            ptable->lenDI = ptable->lenDI + 1; /* There will be a padding field if the directory identifier is an odd number */

        if (IDlen != 0) {
            recGrp_offset = recGrp_offset + (u32)8 + (u32)ptable->lenDI;
            ptable        = (struct iso9660_pathtable *)((u32)ptable + (u32)8 + (u32)ptable->lenDI);
        } else { /* The last path table entry (It's a blank one) in this sector has been reached. Continue searching in the next sector. */
            recLSN++;
            ReadSectors(discimg, sectortype, recLSN, 1, databuf); /* Read the sector containing the next extent. */
            ptable = (struct iso9660_pathtable *)databuf;         /* Reset sector pointer */
        }

        if (name_len > IDlen)
            IDlen = name_len; /* Compare both names, up to the length of the longer string. */

    } while (memcmp(fileEntryName, tgtfname, IDlen)); /* Break out of searching loop if match is found */

searchdirrecords:

    while (!isfilename(&path[pathptr])) { /* While the current segment is not the file's name */
        recLSN = extentLSN;
        ReadSectors(discimg, sectortype, recLSN, 1, databuf); /* Read the sector containing the next extent. */
        dirRec        = (struct iso9660_dirRec *)databuf;     /* Reset sector pointer */
        recGrp_offset = 0;

        memcpy((unsigned char *)&recGrpSz, &dirRec->dataLen_LE, 4);
        DEBUG_PRINTF("Directory record grp sz: 0x%lx\n", recGrpSz); /* Get the size of the entire directory record group */

        stripdirfilename(&path[pathptr], tgtfname, &pathptr); /* "Strip and search..." */
        name_len = strlen(tgtfname);

        do {
            if (((u32)dirRec - (u32)databuf) > recGrpSz) { /* Prevent overflow when file is not found */
                free(databuf);
                return 0;
            }

            DEBUG_PRINTF("Directory record length: 0x%x\n", dirRec->lenDR); /* Get length of directory record */
            memcpy((unsigned char *)&extentLSN, &dirRec->extentLoc_LE, 4);  /* Read extent LSN address */

            DEBUG_PRINTF("Extent LSN: 0x%lx\n", extentLSN);

            IDlen = dirRec->lenFI; /* Get file ID length */
            DEBUG_PRINTF("File ID length: 0x%x\n", IDlen);

#ifdef DEBUG_TTY_FEEDBACK
            memset(fileEntryName, 0, 16); /* 0-fill the file/folder name array */
#endif
            memcpy(fileEntryName, &dirRec->fileIdentifier, IDlen); /* name + extension must be at least 1 character long + ";" (e.g. "n.bin;1") */
            if (IDlen != 0) {
                recGrp_offset = recGrp_offset + (u32)dirRec->lenDR;
                dirRec        = (struct iso9660_dirRec *)((u32)dirRec + (u32)dirRec->lenDR);
            } else { /* The last directory record (It's a blank one) in this sector has been reached. Continue searching in the next sector. */
                recLSN++;
                ReadSectors(discimg, sectortype, recLSN, 1, databuf); /* Read the sector containing the next extent. */
                dirRec = (struct iso9660_dirRec *)databuf;            /* Reset sector pointer */
            }

            DEBUG_PRINTF("Current directory name: %s\n\n", fileEntryName);

            if ((IDlen) && (name_len > IDlen))
                IDlen = name_len;                                     /* Compare both names, up to the length of the longer string. */
        } while ((!IDlen) || memcmp(fileEntryName, tgtfname, IDlen)); /* Break out of searching loop if match is found */
    }

    /* Sector was read into buffer earlier while tracing through directory structure */
    stripdirfilename(&path[pathptr], tgtfname, &pathptr); /* "Strip and search..." */
    name_len = strlen(tgtfname);

    dirRec        = (struct iso9660_dirRec *)databuf;
    recGrp_offset = 0;
    lsn           = extentLSN;

    ReadSectors(discimg, sectortype, extentLSN, 1, databuf); /* Read the sector containing the next extent. */

    memcpy((unsigned char *)&recGrpSz, &dirRec->dataLen_LE, 4);
    DEBUG_PRINTF("Directory record grp sz: 0x%lx\n", recGrpSz); /* Get the size of the entire directory record group */
    do {
        if (recGrp_offset >= recGrpSz) {
            free(databuf);
            return 0; /* Prevent an overflow when file is not found */
        }

        DEBUG_PRINTF("Directory record length: 0x%x\n", dirRec->lenDR);

        memcpy((unsigned char *)&lsn, &dirRec->extentLoc_LE, 4); /* Read the extent's LSN. */
        DEBUG_PRINTF("Extent LSN: 0x%lx\n", lsn);

        memcpy((unsigned char *)file_size, &dirRec->dataLen_LE, 4); /* Copy the size of the extent. */
        DEBUG_PRINTF("Data size: %lu\n", dirRec->dataLen_LE);

        IDlen = dirRec->lenFI; /* Get file ID length */
        DEBUG_PRINTF("File ID length: 0x%x\n", IDlen);

        memset(fileEntryName, 0, 16);                          /* 0-fill the file/folder name array */
        memcpy(fileEntryName, &dirRec->fileIdentifier, IDlen); /* name + extension must be at least 1 character long + ";" (e.g. "n.bin;1") */

        DEBUG_PRINTF("Current filename: %s\n\n", fileEntryName);

        recGrp_offset = recGrp_offset + (u32)dirRec->lenDR;
        dirRec        = (struct iso9660_dirRec *)((u32)dirRec + (u32)dirRec->lenDR); /* Seek to next record */

        if (IDlen == 0) {
            /* The last directory record (It's a blank one) in this sector has been reached. Continue searching in the next sector. */
            extentLSN++;
            ReadSectors(discimg, sectortype, extentLSN, 1, databuf); /* Read the sector containing the next extent. */
            dirRec = (struct iso9660_dirRec *)databuf;               /* Reset sector pointer */
        }

        if ((IDlen) && (name_len > IDlen))
            IDlen = name_len;                                     /* Compare both names, up to the length of the longer string. */
    } while ((!IDlen) || memcmp(fileEntryName, tgtfname, IDlen)); /* Break out of searching loop if match is found. */

    free(databuf);

    /* Put code for returning file's LSN here */
    DEBUG_PRINTF("\n------------------------\n");
    DEBUG_PRINTF("LSN of target file: 0x%lx\n", lsn);

    return lsn;
}

/* Returns information about the disc (Number of sectors (For both layers, if present) and the type of disc). */
int GetDiscInfo(unsigned char source, void *discImg, u32 *nSectorsLayer0, u32 *nSectorsLayer1, unsigned char *discType)
{
    unsigned char *sectorBuff;
    static const unsigned char syncbytes[] = {0x00, 0xFF, 0x0FF, 0x0FF, 0x0FF, 0x0FF, 0x0FF, 0x0FF, 0x0FF, 0x0FF, 0x0FF, 0x00};

    *nSectorsLayer1 = 0;

    if (source == 0) { /* Optical drive. */
        /* NOTE!! The number of sectors on layer 1 can't be determined seperately. */
        return (SystemDriveGetDiscInfo(discImg, nSectorsLayer0, discType));
    } else { /* source==1 -> disc image file. */
        /* Allocate memory to copy a sample of the sectors on the disc. */
        sectorBuff = malloc(2352);

        readFile(discImg, sectorBuff, 12); /* Copy the sync bytes (1st 12 bytes) of the 1st sector as a sample. */
        if (memcmp(syncbytes, sectorBuff, 12) == 0) {
            seekFile(discImg, 15, SEEK_SET); /* Seek to the "mode" byte of the first sector of the disc image file. */
            readFile(discImg, discType, 1);
        } else
            *discType = 0xFF; /* "Headerless" disc image */

        /* Check if the loaded disc/disc image has a ISO9660 format. */
        if (ReadSectors(discImg, *discType, 16, 1, sectorBuff) != 1) { /* Read sector 16. */
            free(sectorBuff);
            return (-1);
        }

        if ((sectorBuff[0] != 0x01) || (memcmp(&sectorBuff[1], "CD001", 5))) {
            free(sectorBuff);
            return (-1);
        }

        memcpy(nSectorsLayer0, &sectorBuff[80], 4); /* Copy the recorded number of sectors in the ISO9660 filesystem. */
    }

    memset(sectorBuff, 0, 2352);                                                /* Flood-fill the temporary buffer with 0s to prevent false DVD9 format disc image detections. */
    if (ReadSectors(discImg, *discType, *nSectorsLayer0, 1, sectorBuff) == 1) { /* Attempt to read the sector after the last sector after the last sector of layer 0. That sector is layer 1's sector #16. */
        if ((sectorBuff[0] == 0x01) && (!memcmp(&sectorBuff[1], "CD001", 5))) { /* The last sector of layer 0 is sector 16 of layer 1. */
            memcpy(nSectorsLayer1, &sectorBuff[80], 4);
        } else {
            if (*nSectorsLayer0 != (seekFile(discImg, 0, SEEK_END) / ((*discType == 0xFF) ? 2048 : 2352))) {
                /*    Somehow, the number of sectors indicated within the ISO9660 filesystem does not match the size of the disc image.
                    There may be trailing data, so calculate the number of sectors based on the disc image file's size instead. */
                *nSectorsLayer0 = (u32)seekFile(discImg, 0, SEEK_END) / ((*discType == 0xFF) ? 2048 : 2352);
            }
        }
    }

    seekFile(discImg, 0, SEEK_SET); /* Seek back to the start of the source image file */

    return 1;
}

static u32 current_lsn = 0;

static int RawReadSectors(void *discImg, unsigned char sectortype, u32 lsn, unsigned int sectors, void *buffer)
{
    unsigned int sectorsToRead;
    unsigned char *bufferPtr;

    sectorsToRead = sectors;
    bufferPtr     = buffer;

    /* ISO9660 MODE 1;        1 sector = 12 bytes "sync" + 4 bytes CRC + 2048 bytes data + 8 bytes reserved space + 280 bytes parity error checking codes. */
    /* Skip the MODE2/XA header;    1 sector = 12 bytes "sync" + 4 bytes CRC + 8 bytes subchannel + 2048 bytes data + 280 bytes parity error checking codes.  */
    /* "Headerless" disc image;    1 sector = 2048 bytes data. */

    if (current_lsn != lsn) { /* Seek only when required. */
        if ((sectortype == 1) || (sectortype == 2)) {
            seekFile(discImg, (unsigned long long int)lsn * 2352, SEEK_SET);
        } else
            seekFile(discImg, (unsigned long long int)lsn * 2048, SEEK_SET);
    }

    if ((sectortype == 1) || (sectortype == 2)) {
        while (sectorsToRead > 0) {
            if (readFile(discImg, bufferPtr, 2352) != 2352)
                break;
            bufferPtr += 2352;
            sectorsToRead--;
        }
    } else {
        if (readFile(discImg, bufferPtr, 2048 * sectorsToRead) == 2048 * sectorsToRead)
            sectorsToRead = 0;
    }

    current_lsn = lsn + (sectors - sectorsToRead); /* Update the current LSN indicator. */

    return (sectors - sectorsToRead);
}

int ReadSectors(void *discImg, unsigned char sectortype, u32 lsn, unsigned int sectors, void *buffer)
{
    unsigned int sectorsToRead;
    unsigned char *bufferPtr, *sectorBuff;

    sectorsToRead = sectors;
    bufferPtr     = buffer;

    /* ISO9660 MODE 1;        1 sector = 12 bytes "sync" + 4 bytes CRC + 2048 bytes data + 8 bytes reserved space + 280 bytes parity error checking codes. */
    /* Skip the MODE2/XA header;    1 sector = 12 bytes "sync" + 4 bytes CRC + 8 bytes subchannel + 2048 bytes data + 280 bytes parity error checking codes.  */
    /* "Headerless" disc image;    1 sector = 2048 bytes data. */

    if (sectortype == 0xFF) { /* "Headerless" disc image. */
        if (RawReadSectors(discImg, sectortype, lsn, sectors, buffer) != sectors)
            return 0;
        sectorsToRead = 0; /* Read all sectors in one shot. */
    } else {               /* MODE 1/2048, or MODE 2/XA Form 1. */
        sectorBuff = malloc(2352);
        while (sectorsToRead > 0) {
            if (RawReadSectors(discImg, sectortype, lsn, 1, sectorBuff) != 1)
                break;

            if (sectortype == 1)
                memcpy(bufferPtr, &sectorBuff[16], 2048); /* Skip sync and mode bytes. */
            else
                memcpy(bufferPtr, &sectorBuff[24], 2048); /* Skip the sync bytes and subchannel data for MODE2/XA FORM 1. */

            sectorsToRead--;
            bufferPtr += 2048;
            lsn++;
        }
        free(sectorBuff);
    }

    return (sectors - sectorsToRead);
}
