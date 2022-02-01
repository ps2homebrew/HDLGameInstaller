#include <errno.h>
#include <Windows.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "HDLGManClient.h"
#include "OSD.h"
#include "install.h"

// Function prototypes
static int ParseIconSysFile(const wchar_t *file, unsigned int length, struct IconSysData *data);

int LoadIconSysFile(const unsigned char *buffer, int size, struct IconSysData *data)
{
    int result, ConvertedLength;
    wchar_t *ConvertedStringBuffer;

    if ((ConvertedStringBuffer = malloc(size * sizeof(wchar_t))) != NULL) {
        ConvertedLength = MultiByteToWideChar(CP_UTF8, 0, (const char *)buffer, size, ConvertedStringBuffer, size);
        result          = ParseIconSysFile(ConvertedStringBuffer, ConvertedLength, data);
        free(ConvertedStringBuffer);
    } else
        result = -ENOMEM;

    return result;
}

static int IsConfigFileLineParam(const wchar_t *line, const wchar_t *ParamName)
{
    unsigned int i;

    for (i = 0; ParamName[i] != '\0'; i++) {
        if (line[i] != ParamName[i]) {
            break;
        }
    }

    return (ParamName[i] == '\0' && !isalpha(line[i]));
}

static wchar_t *ConfigLineTrimLeadingWhitespaces(wchar_t *line)
{
    // Trim whitespaces
    while (*line == ' ')
        line++;

    return line;
}

static wchar_t *GetConfigFileLineParamValue(wchar_t *line)
{
    wchar_t *StartOfValue;

    if ((StartOfValue = wcschr(line, '=')) != NULL) {
        StartOfValue = ConfigLineTrimLeadingWhitespaces(StartOfValue + 1);
    }

    return StartOfValue;
}

static int GetStringValueFromConfigLine(wchar_t *field, unsigned int BufferLength, wchar_t *line)
{
    wchar_t *value;
    int result;

    if ((value = GetConfigFileLineParamValue(line)) != NULL) {
        wcsncpy(field, value, BufferLength - 1);
        field[BufferLength - 1] = '\0';
        result                  = 0;
    } else
        result = -1;

    return result;
}

static int GetXYZFVectorValueFromConfigLine(float *field, wchar_t *line)
{
    wchar_t *value;
    int ValueLength, result;

    if ((value = GetConfigFileLineParamValue(line)) != NULL && ((ValueLength = wcslen(value)) > 0)) {
        field[0] = (float)wcstod(value, &value);
        if (value != NULL && (value = ConfigLineTrimLeadingWhitespaces(value))[0] == ',') {
            field[1] = (float)wcstod(ConfigLineTrimLeadingWhitespaces(value + 1), &value);
            if (value != NULL && (value = ConfigLineTrimLeadingWhitespaces(value))[0] == ',') {
                field[2] = (float)wcstod(ConfigLineTrimLeadingWhitespaces(value + 1), &value);
                result   = 0;
            } else
                result = -1;
        } else
            result = -1;
    } else
        result = -1;

    return result;
}

static int GetXYZIVectorValueFromConfigLine(unsigned char *field, wchar_t *line)
{
    wchar_t *value;
    int ValueLength, result;

    if ((value = GetConfigFileLineParamValue(line)) != NULL && ((ValueLength = wcslen(value)) > 0)) {
        field[0] = (unsigned char)wcstoul(value, &value, 10);
        if (value != NULL && (value = ConfigLineTrimLeadingWhitespaces(value))[0] == ',') {
            field[1] = (unsigned char)wcstoul(ConfigLineTrimLeadingWhitespaces(value + 1), &value, 10);
            if (value != NULL && (value = ConfigLineTrimLeadingWhitespaces(value))[0] == ',') {
                field[2] = (unsigned char)wcstoul(ConfigLineTrimLeadingWhitespaces(value + 1), &value, 10);
                result   = 0;
            } else
                result = -1;
        } else
            result = -1;
    } else
        result = -1;

    return result;
}

static int ParseIconSysFile(const wchar_t *file, unsigned int length, struct IconSysData *data)
{
    int result, ValueLength, TotalFileLengthInBytes;
    wchar_t *line, *value, *buffer;

    memset(data, 0, sizeof(struct IconSysData));

    TotalFileLengthInBytes = length * sizeof(wchar_t);
    if ((buffer = malloc(TotalFileLengthInBytes + sizeof(wchar_t))) != NULL) {
        memcpy(buffer, file, TotalFileLengthInBytes);
        buffer[length] = '\0';

        line = wcstok(buffer, L"\r\n");
        if (line != NULL && wcscmp(line, L"PS2X") == 0) {
            result = 0;
            while ((line = wcstok(NULL, L"\r\n")) != NULL) {
                if (IsConfigFileLineParam(line, L"title0")) {
                    result = GetStringValueFromConfigLine(data->title0, sizeof(data->title0) / sizeof(wchar_t), line);
                } else if (IsConfigFileLineParam(line, L"title1")) {
                    result = GetStringValueFromConfigLine(data->title1, sizeof(data->title1) / sizeof(wchar_t), line);
                } else if (IsConfigFileLineParam(line, L"bgcola")) {
                    if ((value = GetConfigFileLineParamValue(line)) != NULL && ((ValueLength = wcslen(value)) > 0)) {
                        data->bgcola = (unsigned char)wcstoul(value, NULL, 10);
                        result       = 0;
                    } else
                        result = -1;
                } else if (IsConfigFileLineParam(line, L"bgcol0")) {
                    result = GetXYZIVectorValueFromConfigLine(data->bgcol0, line);
                } else if (IsConfigFileLineParam(line, L"bgcol1")) {
                    result = GetXYZIVectorValueFromConfigLine(data->bgcol1, line);
                } else if (IsConfigFileLineParam(line, L"bgcol2")) {
                    result = GetXYZIVectorValueFromConfigLine(data->bgcol2, line);
                } else if (IsConfigFileLineParam(line, L"bgcol3")) {
                    result = GetXYZIVectorValueFromConfigLine(data->bgcol3, line);
                } else if (IsConfigFileLineParam(line, L"lightdir0")) {
                    result = GetXYZFVectorValueFromConfigLine(data->lightdir0, line);
                } else if (IsConfigFileLineParam(line, L"lightdir1")) {
                    result = GetXYZFVectorValueFromConfigLine(data->lightdir1, line);
                } else if (IsConfigFileLineParam(line, L"lightdir2")) {
                    result = GetXYZFVectorValueFromConfigLine(data->lightdir2, line);
                } else if (IsConfigFileLineParam(line, L"lightcolamb")) {
                    result = GetXYZIVectorValueFromConfigLine(data->lightcolamb, line);
                } else if (IsConfigFileLineParam(line, L"lightcol0")) {
                    result = GetXYZIVectorValueFromConfigLine(data->lightcol0, line);
                } else if (IsConfigFileLineParam(line, L"lightcol1")) {
                    result = GetXYZIVectorValueFromConfigLine(data->lightcol1, line);
                } else if (IsConfigFileLineParam(line, L"lightcol2")) {
                    result = GetXYZIVectorValueFromConfigLine(data->lightcol2, line);
                } else if (IsConfigFileLineParam(line, L"uninstallmes0")) {
                    result = GetStringValueFromConfigLine(data->uninstallmes0, sizeof(data->uninstallmes0) / sizeof(wchar_t), line);
                } else if (IsConfigFileLineParam(line, L"uninstallmes1")) {
                    result = GetStringValueFromConfigLine(data->uninstallmes1, sizeof(data->uninstallmes1) / sizeof(wchar_t), line);
                } else if (IsConfigFileLineParam(line, L"uninstallmes2")) {
                    result = GetStringValueFromConfigLine(data->uninstallmes2, sizeof(data->uninstallmes2) / sizeof(wchar_t), line);
                } else {
                    result = -1;
                }

                if (result != 0) {
                    break;
                }
            }
        } else {
            result = -EINVAL;
        }

        free(buffer);
    } else
        result = -ENOMEM;

    return result;
}

int GenerateHDDIconSysFile(const struct IconSysData *data, char *HDDIconSys, unsigned int OutputBufferLength)
{
    wchar_t buffer[512];

    swprintf(buffer, OutputBufferLength,
             L"PS2X\n"
             L"title0 = %s\n"
             L"title1 = %s\n"
             L"bgcola = %u\n"
             L"bgcol0 = %u,%u,%u\n"
             L"bgcol1 = %u,%u,%u\n"
             L"bgcol2 = %u,%u,%u\n"
             L"bgcol3 = %u,%u,%u\n"
             L"lightdir0 = %1.4f,%1.4f,%1.4f\n"
             L"lightdir1 = %1.4f,%1.4f,%1.4f\n"
             L"lightdir2 = %1.4f,%1.4f,%1.4f\n"
             L"lightcolamb = %u,%u,%u\n"
             L"lightcol0 = %u,%u,%u\n"
             L"lightcol1 = %u,%u,%u\n"
             L"lightcol2 = %u,%u,%u\n"
             L"uninstallmes0 = %s\n"
             L"uninstallmes1 = %s\n"
             L"uninstallmes2 = %s\n",
             data->title0, // Title line 1 is mandatory.
             data->title1,
             data->bgcola,
             data->bgcol0[0], data->bgcol0[1], data->bgcol0[2],
             data->bgcol1[0], data->bgcol1[1], data->bgcol1[2],
             data->bgcol2[0], data->bgcol2[1], data->bgcol2[2],
             data->bgcol3[0], data->bgcol3[1], data->bgcol3[2],
             data->lightdir0[0], data->lightdir0[1], data->lightdir0[2],
             data->lightdir1[0], data->lightdir1[1], data->lightdir1[2],
             data->lightdir2[0], data->lightdir2[1], data->lightdir2[2],
             data->lightcolamb[0], data->lightcolamb[1], data->lightcolamb[2],
             data->lightcol0[0], data->lightcol0[1], data->lightcol0[2],
             data->lightcol1[0], data->lightcol1[1], data->lightcol1[2],
             data->lightcol2[0], data->lightcol2[1], data->lightcol2[2],
             data->uninstallmes0, data->uninstallmes1, data->uninstallmes2);

    return WideCharToMultiByte(CP_UTF8, 0, buffer, -1, HDDIconSys, OutputBufferLength, NULL, NULL);
}

int GenerateHDDIconSysFileFromMCSave(const mcIcon *McIconSys, char *HDDIconSys, unsigned int OutputBufferLength, const wchar_t *title1, const wchar_t *title2)
{
    struct IconSysData IconSysData;

    wcsncpy(IconSysData.title0, title1, sizeof(IconSysData.title0) - 1);
    IconSysData.title0[sizeof(IconSysData.title0) - 1] = '\0';

    // Line 2 is optional.
    if (title2 != NULL) {
        wcsncpy(IconSysData.title1, title2, sizeof(IconSysData.title1) - 1);
        IconSysData.title1[sizeof(IconSysData.title1) - 1] = '\0';
    } else
        IconSysData.title1[0] = '\0';
    IconSysData.bgcola         = McIconSys->trans;
    IconSysData.bgcol0[0]      = McIconSys->bgCol[0][0] / 2;
    IconSysData.bgcol0[1]      = McIconSys->bgCol[0][1] / 2;
    IconSysData.bgcol0[2]      = McIconSys->bgCol[0][2] / 2;
    IconSysData.bgcol1[0]      = McIconSys->bgCol[1][0] / 2;
    IconSysData.bgcol1[1]      = McIconSys->bgCol[1][1] / 2;
    IconSysData.bgcol1[2]      = McIconSys->bgCol[1][2] / 2;
    IconSysData.bgcol2[0]      = McIconSys->bgCol[2][0] / 2;
    IconSysData.bgcol2[1]      = McIconSys->bgCol[2][1] / 2;
    IconSysData.bgcol2[2]      = McIconSys->bgCol[2][2] / 2;
    IconSysData.bgcol3[0]      = McIconSys->bgCol[3][0] / 2;
    IconSysData.bgcol3[1]      = McIconSys->bgCol[3][1] / 2;
    IconSysData.bgcol3[2]      = McIconSys->bgCol[3][2] / 2;
    IconSysData.lightdir0[0]   = McIconSys->lightDir[0][0];
    IconSysData.lightdir0[1]   = McIconSys->lightDir[0][1];
    IconSysData.lightdir0[2]   = McIconSys->lightDir[0][2];
    IconSysData.lightdir1[0]   = McIconSys->lightDir[1][0];
    IconSysData.lightdir1[1]   = McIconSys->lightDir[1][1];
    IconSysData.lightdir1[2]   = McIconSys->lightDir[1][2];
    IconSysData.lightdir2[0]   = McIconSys->lightDir[2][0];
    IconSysData.lightdir2[1]   = McIconSys->lightDir[2][1];
    IconSysData.lightdir2[2]   = McIconSys->lightDir[2][2];
    IconSysData.lightcolamb[0] = (unsigned char)(McIconSys->lightAmbient[0] * 128);
    IconSysData.lightcolamb[1] = (unsigned char)(McIconSys->lightAmbient[1] * 128);
    IconSysData.lightcolamb[2] = (unsigned char)(McIconSys->lightAmbient[2] * 128);
    IconSysData.lightcol0[0]   = (unsigned char)(McIconSys->lightCol[0][0] * 128);
    IconSysData.lightcol0[1]   = (unsigned char)(McIconSys->lightCol[0][1] * 128);
    IconSysData.lightcol0[2]   = (unsigned char)(McIconSys->lightCol[0][2] * 128);
    IconSysData.lightcol1[0]   = (unsigned char)(McIconSys->lightCol[1][0] * 128);
    IconSysData.lightcol1[1]   = (unsigned char)(McIconSys->lightCol[1][1] * 128);
    IconSysData.lightcol1[2]   = (unsigned char)(McIconSys->lightCol[1][2] * 128);
    IconSysData.lightcol2[0]   = (unsigned char)(McIconSys->lightCol[2][0] * 128);
    IconSysData.lightcol2[1]   = (unsigned char)(McIconSys->lightCol[2][1] * 128);
    IconSysData.lightcol2[2]   = (unsigned char)(McIconSys->lightCol[2][2] * 128);
    wcscpy(IconSysData.uninstallmes0, L"This will delete the game.");
    IconSysData.uninstallmes1[0] = '\0';
    IconSysData.uninstallmes2[0] = '\0';

    // DEBUG_PRINTF("MC Save title: %s. New title: %s,%s\n", (char *)McIconSys->title, title1, title2);
    return GenerateHDDIconSysFile(&IconSysData, HDDIconSys, OutputBufferLength);
}

int LoadMcSaveSysFromPath(const wchar_t *SaveFilePath, mcIcon *McSaveIconSys)
{
    FILE *IconSysFile;
    int result;

    if ((IconSysFile = _wfopen(SaveFilePath, L"rb")) != NULL) {
        result = fread(McSaveIconSys, sizeof(mcIcon), 1, IconSysFile);
        fclose(IconSysFile);

        if (result == 1) {
            result = 0;
        } else
            result = -EIO;
    } else {
        result = -ENOENT;
        //	DEBUG_PRINTF("Memory card save file not found: %d\n", result);
    }

    return result;
}

static int ReadFileIntoBuffer(const wchar_t *path, void **buffer)
{
    FILE *file;
    int result, length;

    if ((file = _wfopen(path, L"rb")) != NULL) {
        fseek(file, 0, SEEK_END);
        length = ftell(file);
        rewind(file);
        if ((*buffer = malloc(length)) != NULL) {
            if (fread(*buffer, 1, length, file) == length) {
                result = 0;
            } else {
                free(*buffer);
                result = -EIO;
            }
        } else
            result = -ENOMEM;

        fclose(file);
    } else
        result = -ENOENT;

    return (result < 0 ? result : length);
}

static int IsFileExists(const wchar_t *path)
{
    FILE *file;
    int result;

    result = 0;
    if ((file = _wfopen(path, L"rb")) != NULL) {
        result = 1;
        fclose(file);
    }

    return result;
}

int VerifyMcSave(const wchar_t *SaveFolderPath)
{
    wchar_t *path;
    FILE *file;
    int result, SaveFolderPathLength, PathBufferLength;
    mcIcon McIconSys;

    result               = 0;
    SaveFolderPathLength = wcslen(SaveFolderPath);
    if ((path = malloc((PathBufferLength = sizeof(wchar_t) * (SaveFolderPathLength + 34)))) != NULL) {
        swprintf(path, PathBufferLength, L"%s/icon.sys", SaveFolderPath);

        if ((file = _wfopen(path, L"rb")) != NULL) {
            result = fread(&McIconSys, sizeof(McIconSys), 1, file) == 1 ? 0 : -EIO;
            fclose(file);

            if (result == 0) {
                MultiByteToWideChar(CP_ACP, 0, McIconSys.view, -1, &path[SaveFolderPathLength + 1], 33);
                if (IsFileExists(path)) {
                    if (McIconSys.del[0] != '\0') {
                        MultiByteToWideChar(CP_ACP, 0, McIconSys.del, -1, &path[SaveFolderPathLength + 1], 33);
                        result = (IsFileExists(path)) ? 0 : -ENOENT;
                    } else
                        result = 0;
                } else
                    result = -ENOENT;
            }
        } else
            result = -ENOENT;

        free(path);
    } else
        result = -ENOMEM;

    return result;
}

int ConvertMcSave(const wchar_t *SaveFolderPath, struct ConvertedMcIcon *ConvertedIcon, const wchar_t *OSDTitleLine1, const wchar_t *OSDTitleLine2)
{
    wchar_t *path;
    FILE *file;
    int result, SaveFolderPathLength, PathBufferLength;
    mcIcon McIconSys;

    memset(ConvertedIcon, 0, sizeof(struct ConvertedMcIcon));

    SaveFolderPathLength = wcslen(SaveFolderPath);
    if ((path = malloc((PathBufferLength = sizeof(wchar_t) * (SaveFolderPathLength + 34)))) != NULL) {
        swprintf(path, PathBufferLength, L"%s/icon.sys", SaveFolderPath);

        if ((file = _wfopen(path, L"rb")) != NULL) {
            result = fread(&McIconSys, sizeof(McIconSys), 1, file) == 1 ? 0 : -EIO;
            fclose(file);

            if (result == 0) {
                MultiByteToWideChar(CP_ACP, 0, McIconSys.view, -1, &path[SaveFolderPathLength + 1], 33);
                if ((result = ReadFileIntoBuffer(path, &ConvertedIcon->ListViewIcon)) > 0) {
                    ConvertedIcon->ListViewIconSize = result;

                    if (McIconSys.del[0] != '\0' && strcmp(McIconSys.del, McIconSys.view) != 0) {
                        MultiByteToWideChar(CP_ACP, 0, McIconSys.del, -1, &path[SaveFolderPathLength + 1], 33);
                        if ((result = ReadFileIntoBuffer(path, &ConvertedIcon->DeleteIcon)) > 0) {
                            ConvertedIcon->DeleteIconSize = result;
                            result                        = 0;
                        }
                    } else
                        result = 0;

                    if (result == 0) {
                        if ((ConvertedIcon->HDDIconSys = malloc(640)) != NULL) {
                            if ((result = GenerateHDDIconSysFileFromMCSave(&McIconSys, ConvertedIcon->HDDIconSys, 640, OSDTitleLine1, OSDTitleLine2)) > 0) {
                                ConvertedIcon->HDDIconSysSize = result;

                                if ((ConvertedIcon->HDDIconSys = realloc(ConvertedIcon->HDDIconSys, ConvertedIcon->HDDIconSysSize)) != NULL) {
                                    result = 0;
                                } else
                                    result = -ENOMEM;
                            }
                        } else
                            result = -ENOMEM;
                    }
                }
            }
        } else
            result = -ENOENT;

        free(path);
    } else
        result = -ENOMEM;

    if (result < 0)
        FreeConvertedMcSave(ConvertedIcon);

    return result;
}

void FreeConvertedMcSave(struct ConvertedMcIcon *ConvertedIcon)
{
    if (ConvertedIcon->HDDIconSys != NULL)
        free(ConvertedIcon->HDDIconSys);
    if (ConvertedIcon->ListViewIcon != NULL)
        free(ConvertedIcon->ListViewIcon);
    if (ConvertedIcon->DeleteIcon != NULL)
        free(ConvertedIcon->DeleteIcon);

    memset(ConvertedIcon, 0, sizeof(struct ConvertedMcIcon));
}

int InstallGameInstallationOSDResources(const char *partition, const char *DiscID, const struct GameSettings *GameSettings, const struct ConvertedMcIcon *ConvertedIcon)
{
    int result;

    if (GameSettings->IconSource != 1) {
        if ((result = HDLGManInitOSDResources(partition, DiscID, GameSettings->OSDTitleLine1, GameSettings->OSDTitleLine2, 0)) >= 0) {
            if (GameSettings->IconSource == 2) {
                if ((result = HDLGManOSDResourceLoad(OSD_ICON_SYS_INDEX, ConvertedIcon->HDDIconSys, ConvertedIcon->HDDIconSysSize)) == 0) {
                    if ((result = HDLGManOSDResourceLoad(OSD_VIEW_ICON_INDEX, ConvertedIcon->ListViewIcon, ConvertedIcon->ListViewIconSize)) == 0) {
                        if (ConvertedIcon->DeleteIconSize > 0) {
                            result = HDLGManOSDResourceLoad(OSD_VIEW_ICON_INDEX, ConvertedIcon->DeleteIcon, ConvertedIcon->DeleteIconSize);
                        }
                    }
                }

                if (result == 0) {
                    if ((result = HDLGManWriteOSDResources()) == 0)
                        result = 1;
                }
            } else {
                result = HDLGManWriteOSDResources();
            }
        }
    } else {
        if (HDLGManInitOSDResources(partition, DiscID, GameSettings->OSDTitleLine1, GameSettings->OSDTitleLine2, 1) == 1) {
            if ((result = HDLGManWriteOSDResources()) == 0)
                result = 1;
        } else {
            HDLGManOSDResourceWriteCancel();
            result = 0;
        }
    }

    return result;
}

/* static int DumpFile(const wchar_t *filename, const void *buffer, int length){
    FILE *file;
    int result;

    if((file=_wfopen(filename, L"wb"))!=NULL){
        result=fwrite(buffer, 1, length, file)==length?0:EIO;
        fclose(file);
    }
    else result=EIO;

    return result;
} */

int UpdateGameInstallationOSDResources(const char *partition, const wchar_t *title1, const wchar_t *title2)
{
    void *FileBuffers[NUM_OSD_FILES_ENTS];
    struct OSDResourceStat ResourceStats;
    int result, i;
    struct IconSysData IconSys;
    unsigned char *HDDIconSys;

    if ((result = HDLGManGetOSDResourcesStat(partition, &ResourceStats)) == 0) {
        memset(FileBuffers, 0, sizeof(FileBuffers));

        for (i = 0, result = 0; i < NUM_OSD_FILES_ENTS; i++) {
            if (ResourceStats.lengths[i] > 0) {
                if ((FileBuffers[i] = malloc(ResourceStats.lengths[i])) != NULL) {
                    if ((result = HDLGManReadOSDResourceFile(partition, i, FileBuffers[i], ResourceStats.lengths[i])) != 0) {
                        break;
                    }
                } else {
                    result = -ENOMEM;
                    break;
                }
            }
        }

        if (result == 0 && FileBuffers[OSD_ICON_SYS_INDEX] != NULL) {
            if ((result = LoadIconSysFile(FileBuffers[OSD_ICON_SYS_INDEX], ResourceStats.lengths[OSD_ICON_SYS_INDEX], &IconSys)) == 0) {
                // Update icon sys file.
                wcscpy(IconSys.title0, title1);
                wcscpy(IconSys.title1, title2);

                /* DumpFile(L"icon_sys.txt", FileBuffers[OSD_ICON_SYS_INDEX], ResourceStats.lengths[OSD_ICON_SYS_INDEX]);
                DumpFile(L"ViewIcon.icn", FileBuffers[OSD_VIEW_ICON_INDEX], ResourceStats.lengths[OSD_VIEW_ICON_INDEX]); */

                free(FileBuffers[OSD_ICON_SYS_INDEX]);

                /* Convert the icon file back into the HDD OSD format. */
                HDDIconSys                      = malloc(640); /* Allocate sufficient memory to accommodate the longest possible title and the standard icon.sys file fields as in the template. */
                FileBuffers[OSD_ICON_SYS_INDEX] = malloc(ResourceStats.lengths[OSD_ICON_SYS_INDEX] = GenerateHDDIconSysFile(&IconSys, HDDIconSys, 640) - 1);
                memcpy(FileBuffers[OSD_ICON_SYS_INDEX], HDDIconSys, ResourceStats.lengths[OSD_ICON_SYS_INDEX]);
                free(HDDIconSys);

                if ((result = HDLGManInitOSDDefaultResources(partition)) == 0) {
                    for (i = 0; i < NUM_OSD_FILES_ENTS; i++) {
                        if (FileBuffers[i] != NULL) {
                            HDLGManOSDResourceLoad(i, FileBuffers[i], ResourceStats.lengths[i]);
                        }
                    }

                    // Write updated file back (Rebuild the partition attribute area).
                    result = HDLGManWriteOSDResources();
                }
            } else { // Can't parse the icon.sys file.
                for (i = 0; i < NUM_OSD_FILES_ENTS; i++) {
                    if (FileBuffers[i] != NULL)
                        free(FileBuffers[i]);
                }
            }
        } else {
            result = -1; // partition attribute area is corrupted.
            for (i = 0; i < NUM_OSD_FILES_ENTS; i++) {
                if (FileBuffers[i] != NULL)
                    free(FileBuffers[i]);
            }
        }
    } else {
        // DEBUG_PRINTF("UpdateGameInstallationOSDResources: Can't getstat the OSD resource files: %d\n", result);
    }

    return result;
}
