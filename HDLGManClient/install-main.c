#include <errno.h>
#include <Windows.h>
#include <Shlobj.h>
#include <Commctrl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "HDLGManClient.h"
#include "client.h"
#include "main.h"
#include "system.h"
#include "install.h"
#include "io.h"
#include "iso9660.h"
#include "OSD.h"
#include "resource.h"

int InstallerThreadCommandParam;

// Attempt to reconnect the command socket.
static int AttemptReconnectCmd(void)
{
    int result, reconnects;

    for (reconnects = 0; reconnects < RECONNECT_COUNT; reconnects++) {
        if ((result = ReconnectToServer()) == 0) { // Successfully re-connected.
            break;
        }
    }

    if (result != 0)
        result = -EEXTCONNLOST;

    return result;
}

static DWORD WINAPI InstallerThread(struct InstallerThreadParams *params)
{
    char DiscID[11], StartupFilename[32], partition[33];
    wchar_t TextBuffer[16];
    void *discimg;
    const void *buffer;
    u32 lsn, sectorcount, dl_dvd_sectors, SectorsToRead;
    u8 sectortype, CurrentProgressPercentage, OldProgressPercentage;
    double TimeElasped;
    int result, installResult, retries, reconnects, JobIndex;
    time_t CurrentTime, StartTime, PreviousCheckTime;
    unsigned int speed, SectorsPerSecond, HoursRemaining, SecondsRemaining, TimeElaspedBlock;
    unsigned long int BlockStartTime;
    u8 MinutesRemaining, XFerMode, XFerType;
    struct ConvertedMcIcon ConvertedIcon;
    struct GameSettings *GameSettings;
    struct BuffDesc *bds;
    u8 *ioBuffers;

    ioBuffers = NULL;
    bds       = NULL;

    // Initialize the progress bars
    SendMessage(GetDlgItem(params->ParentDialog, IDC_PROGRESS_INSTALL_ALL), PBM_SETRANGE, 0, MAKELPARAM(0, params->jobs->count));
    SendMessage(GetDlgItem(params->ParentDialog, IDC_PROGRESS_INSTALL), PBM_SETRANGE, 0, MAKELPARAM(0, 100));

    for (JobIndex = 0, GameSettings = params->jobs->games, result = 0; JobIndex < params->jobs->count && result >= 0; JobIndex++, GameSettings++) {
        // Update the global progress bar.
        if (JobIndex != 0)
            SendMessage(GetDlgItem(params->ParentDialog, IDC_PROGRESS_INSTALL_ALL), PBM_DELTAPOS, (WPARAM)1, 0);
        swprintf(TextBuffer, sizeof(TextBuffer) / sizeof(wchar_t), L"%u%%", JobIndex * 100 / params->jobs->count);
        SetWindowText(GetDlgItem(params->ParentDialog, IDC_STATIC_PROGRESS_ALL), TextBuffer);

        // Reset the progress bar.
        SetWindowText(GetDlgItem(params->ParentDialog, IDC_STATIC_PROGRESS), L"0%");
        SendMessage(GetDlgItem(params->ParentDialog, IDC_PROGRESS_INSTALL), PBM_SETPOS, 0, 0);

        // Reset the speed and time remaining indicators.
        SetWindowText(GetDlgItem(params->ParentDialog, IDC_STATIC_TIME_REMAINING), L"--:--:--");
        SetWindowText(GetDlgItem(params->ParentDialog, IDC_STATIC_AVERAGE_SPEED), L"0 bytes/s");
        SetWindowText(GetDlgItem(params->ParentDialog, IDC_STATIC_CURRENT_SPEED), L"0 bytes/s");

        // Begin game install process.
        discimg = openFile(GameSettings->SourcePath, O_RDONLY);

        if (discimg != NULL) {
            /* Initilize local variables */
            sectorcount = 0; /* Number of sectors in the source disc image */
            result      = 0;

            if ((result = GetDiscInfo(GameSettings->source, discimg, &sectorcount, &dl_dvd_sectors, &sectortype)) >= 0) {
                if (!ParsePS2CNF(discimg, DiscID, StartupFilename, NULL, sectortype)) {
                    displayAlertMsg(L"Error parsing SYSTEM.CNF on disc.\n");
                    closeFile(discimg);
                    result = -EINVAL;
                    goto InstallEnd;
                }

                if (HDLGManGetGamePartName(DiscID, partition) == 0) {
                    if (MessageBox(params->ParentDialog, L"Game is already installed. Overwrite?", L"Game already installed", MB_YESNO | MB_ICONQUESTION) == IDNO) {
                        result = 1;
                        goto InstallEnd;
                    }

                    HDLGManDeleteGameEntry(partition);
                }

                if (GameSettings->UseMDMA0) {
                    XFerType = ATA_XFER_MODE_MDMA;
                    XFerMode = 0;
                } else {
                    XFerType = ATA_XFER_MODE_UDMA;
                    XFerMode = 4;
                }

                if ((result = HDLGManPrepareGameInstall(GameSettings->FullTitle, DiscID, StartupFilename, GameSettings->DiscType, sectorcount, dl_dvd_sectors, GameSettings->CompatibilityModeFlags, XFerType, XFerMode)) < 0) {
                    if (result == -EEXTCONNLOST) {
                        displayAlertMsg(L"The connection to the console has been lost.\nPlease reconnect.\n");
                    } else {
                        displayAlertMsg(L"Error occurred while preparing for game installation.\n");
                    }
                    closeFile(discimg);
                    goto InstallEnd;
                }

                seekFile(discimg, 0, SEEK_SET); /* Seek back to the start of the source image file */

                /* Allocate memory to copy sectors. */
                if (IOAlloc(&bds, (void **)&ioBuffers) != 0) {
                    displayAlertMsg(L"Failure allocating memory.\n");
                    closeFile(discimg);
                    result = -ENOMEM;
                    goto InstallEnd;
                }

                IOReadInit(discimg, bds, ioBuffers, IO_BANKSIZE, IO_BANKMAX, sectorcount + dl_dvd_sectors, sectortype);

                PreviousCheckTime = StartTime = time(NULL);
                BlockStartTime                = timemsec();
                CurrentProgressPercentage = OldProgressPercentage = 0;
                for (lsn = 0; lsn < sectorcount + dl_dvd_sectors; lsn += SectorsToRead) {
                    if (InstallerThreadCommandParam != INSTALLER_CMD_NONE) {
                        switch (InstallerThreadCommandParam) {
                            case INSTALLER_CMD_ABORT:
                                MessageBox(params->ParentDialog, L"Game installation aborted.", L"Installation cancelled", MB_OK | MB_ICONINFORMATION);
                                result = -EEXTABORT;
                                goto GameCopy_end;
                                break;
                        }
                    }

                    SectorsToRead = sectorcount + dl_dvd_sectors - lsn > IO_BUFFER_SIZE ? IO_BUFFER_SIZE : sectorcount + dl_dvd_sectors - lsn;

                    if (IOGetStatus() == IO_THREAD_STATE_ERROR) {
                        displayAlertMsg(L"Error reading sectors!\n");
                        result = -EIO;
                        break;
                    }
                    IOReadNext(&buffer);

                    BlockStartTime = timemsec();
                    for (retries = 0; retries < RETRY_COUNT; retries++) {
                        result = HDLGManWriteGame(buffer, 2048 * SectorsToRead);

                        if (result == 0) { // No error.
                            result = 0;
                            break;
                        } else {
                            // Terminate and reconnect (data connection was either lost or closed by remote peer).
                            CloseDataConnection();

                            result = HDLGManGetIOStatus();

                            if (result == 0) {                    // All OK. Only the data connection was disconnected.
                            } else if (result == -EEXTCONNLOST) { // Command socket disconnected. Attempt to reconnect.
                                result = AttemptReconnectCmd();

                                if (result != 0)
                                    break;
                            } else { // Some other error, like disk I/O error (non-recoverable).
                                displayAlertMsg(L"Error writing to game: %d\n", result);
                                result = -EIO;
                                break;
                            }

                            if (result == 0) { // Command connection is okay. Attempt to reconnect for data connection.
                                for (reconnects = 0; reconnects < RECONNECT_COUNT; reconnects++) {
                                    if ((result = ReconnectToServerForWrite(DiscID, sectorcount + dl_dvd_sectors - lsn, lsn)) == 0) { // Successfully re-connected.
                                        break;
                                    }
                                }

                                if (result != 0) {
                                    result = -EEXTCONNLOST;
                                    break;
                                }
                            }
                        }
                    }

                    if (result == -EEXTCONNLOST)
                        displayAlertMsg(L"The connection to the console has been lost.\nPlease reconnect.\n");

                    if (result != 0)
                        break;

                    IOReadAdvance();

                    TimeElaspedBlock = diffmsec(BlockStartTime, timemsec());
                    CurrentTime      = time(NULL);
                    if (difftime(CurrentTime, PreviousCheckTime) > 0) {
                        if (TimeElaspedBlock > 0) {
                            speed = (unsigned int)(SectorsToRead * (2048 / 1024) * 1000 / TimeElaspedBlock);
                            swprintf(TextBuffer, sizeof(TextBuffer) / sizeof(wchar_t), L"%u KB/s", speed);
                            SetWindowText(GetDlgItem(params->ParentDialog, IDC_STATIC_CURRENT_SPEED), TextBuffer);
                        }

                        CurrentProgressPercentage = (unsigned char)(lsn * 100 / (sectorcount + dl_dvd_sectors));
                        if (CurrentProgressPercentage != OldProgressPercentage) {
                            SendMessage(GetDlgItem(params->ParentDialog, IDC_PROGRESS_INSTALL), PBM_SETPOS, (WPARAM)CurrentProgressPercentage, 0);
                            OldProgressPercentage = CurrentProgressPercentage;

                            swprintf(TextBuffer, sizeof(TextBuffer) / sizeof(wchar_t), L"%u%%", CurrentProgressPercentage);
                            SetWindowText(GetDlgItem(params->ParentDialog, IDC_STATIC_PROGRESS), TextBuffer);
                        }

                        if ((TimeElasped = difftime(CurrentTime, StartTime)) > 0) {
                            if ((speed = (unsigned int)(lsn * (2048 / 1024) / TimeElasped)) > 0 && (SectorsPerSecond = speed * 1024 / 2048) > 0) {
                                swprintf(TextBuffer, sizeof(TextBuffer) / sizeof(wchar_t), L"%u KB/s", speed);
                                SetWindowText(GetDlgItem(params->ParentDialog, IDC_STATIC_AVERAGE_SPEED), TextBuffer);

                                SecondsRemaining = (sectorcount + dl_dvd_sectors - lsn) / SectorsPerSecond;
                                HoursRemaining   = SecondsRemaining / 3600;
                                MinutesRemaining = (SecondsRemaining - HoursRemaining * 3600) / 60;
                                if (SecondsRemaining < UINT_MAX) {
                                    swprintf(TextBuffer, sizeof(TextBuffer) / sizeof(wchar_t), L"%02u:%02u:%02u", HoursRemaining, MinutesRemaining, SecondsRemaining - HoursRemaining * 3600 - MinutesRemaining * 60);
                                } else {
                                    wcscpy(TextBuffer, L"--:--:--");
                                }

                                SetWindowText(GetDlgItem(params->ParentDialog, IDC_STATIC_TIME_REMAINING), TextBuffer);
                            } else {
                                SetWindowText(GetDlgItem(params->ParentDialog, IDC_STATIC_AVERAGE_SPEED), L"0 bytes/s");
                                SetWindowText(GetDlgItem(params->ParentDialog, IDC_STATIC_TIME_REMAINING), L"--:--:--");
                            }
                        }

                        PreviousCheckTime = CurrentTime;
                    }
                } /* End of main loop */
            GameCopy_end:
                installResult = result;

                IOEndRead();
                closeFile(discimg);

                IOFree(&bds, (void **)&ioBuffers);
                CloseDataConnection(); // Regardless of how it went, close the data connection.

                // Do not attempt to close with a closed socket.
                if (result != -EEXTCONNLOST) {
                    for (retries = 0; retries < RETRY_COUNT; retries++) {
                        result = HDLGManCloseGame();

                        if (result >= 0) { // All OK.
                            break;
                        } else if (result == -EEXTCONNLOST) { // Command socket disconnected. Attempt to reconnect.
                            result = AttemptReconnectCmd();

                            if (result != 0)
                                break;
                        } else { // Some other error, like disk I/O error (non-recoverable).
                            displayAlertMsg(L"Error closing game: %d\n", result);
                            break;
                        }
                    }

                    if (result == -EEXTCONNLOST)
                        displayAlertMsg(L"The connection to the console has been lost.\nPlease reconnect.\n");

                    if (result < 0)
                        installResult = result;
                }

                if (installResult >= 0) {
                    if (HDLGManGetGamePartName(DiscID, partition) != 0) {
                        MessageBox(params->ParentDialog, L"Game was not installed properly.", L"Error", MB_OK | MB_ICONERROR);
                        result = -ENOENT;
                        goto InstallEnd;
                    }

                    if (GameSettings->IconSource == 2) {
                        if (ConvertMcSave(GameSettings->IconSourcePath, &ConvertedIcon, GameSettings->OSDTitleLine1, GameSettings->OSDTitleLine2) != 0) {
                            MessageBox(params->ParentDialog, L"Failed to load icon from savedata. The default icon will be used.", L"Failed to load icon", MB_OK | MB_ICONWARNING);
                            GameSettings->IconSource = 0;
                            memset(&ConvertedIcon, 0, sizeof(ConvertedIcon));
                        }
                    } else
                        memset(&ConvertedIcon, 0, sizeof(ConvertedIcon));

                RetryIconInstall:
                    if ((result = InstallGameInstallationOSDResources(partition, DiscID, GameSettings, &ConvertedIcon)) >= 0) {
                        if (result != 1 && (GameSettings->IconSource == 1 || GameSettings->IconSource == 2)) {
                            MessageBox(params->ParentDialog, L"Failed to load icon from savedata. The default icon will be used.", L"Failed to load icon", MB_OK | MB_ICONWARNING);
                            GameSettings->IconSource = 0;
                            goto RetryIconInstall;
                        }
                    } else {
                        displayAlertMsg(L"Error writing OSD resources: %d\n", result);
                    }

                    if (result < 0)
                        HDLGManDeleteGameEntry(partition);

                    FreeConvertedMcSave(&ConvertedIcon);
                } else {
                    if (HDLGManGetGamePartName(DiscID, partition) == 0) {
                        HDLGManDeleteGameEntry(partition);
                    }

                    result = installResult; // Restore the installation result (the reason for failure).
                }
            } else {
                displayAlertMsg(L"Unsupported disc type, or there was an error reading the disc's information.\n");
            }
        } else {
            displayAlertMsg(L"Unable to open disc image file %s.\n", GameSettings->SourcePath);
            result = -ENOENT;
        }
    }

InstallEnd:
    if (result >= 0) {
        MessageBox(params->ParentDialog, L"Games installed successfully.", L"Installation complete.", MB_OK | MB_ICONINFORMATION);
        EndDialog(params->ParentDialog, TRUE);
    }

    EnableWindow(GetDlgItem(params->ParentDialog, IDCANCEL), TRUE);
    EnableWindow(GetDlgItem(params->ParentDialog, IDABORT), FALSE);
    return 0;
}

static DWORD WINAPI RetrieveThread(struct RetrieveThreadParams *params)
{
    wchar_t TextBuffer[16];
    void *discimg, *wrBuffer;
    u32 lsn, SectorsToRead;
    unsigned char CurrentProgressPercentage, OldProgressPercentage;
    double TimeElasped;
    int result, installResult, retries, reconnects;
    time_t CurrentTime, StartTime, PreviousCheckTime;
    unsigned long int BlockStartTime;
    unsigned int speed, SectorsPerSecond, HoursRemaining, SecondsRemaining, TimeElaspedBlock;
    unsigned char MinutesRemaining;
    struct BuffDesc *bds;
    u8 *ioBuffers;

    ioBuffers = NULL;
    bds       = NULL;

    // Reset the progress bar.
    SendMessage(GetDlgItem(params->ParentDialog, IDC_PROGRESS_COPY), PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SetWindowText(GetDlgItem(params->ParentDialog, IDC_STATIC_PROGRESS), L"0%");

    // Reset the speed and time remaining indicators.
    SetWindowText(GetDlgItem(params->ParentDialog, IDC_STATIC_TIME_REMAINING), L"--:--:--");
    SetWindowText(GetDlgItem(params->ParentDialog, IDC_STATIC_AVERAGE_SPEED), L"0 bytes/s");
    SetWindowText(GetDlgItem(params->ParentDialog, IDC_STATIC_CURRENT_SPEED), L"0 bytes/s");

    if ((result = HDLGManInitGameRead(params->partition, params->sectors, 0)) >= 0) {
        // Begin game install process.
        discimg = openFile(params->destination, O_WRONLY | O_CREAT | O_TRUNC);

        if (discimg != NULL) {
            result = 0;

            /* Allocate memory to copy sectors. */
            if (IOAlloc(&bds, (void **)&ioBuffers) != 0) {
                displayAlertMsg(L"Failure allocating memory.\n");
                closeFile(discimg);
                result = -ENOMEM;
                goto CopyEnd;
            }

            IOWriteInit(discimg, bds, ioBuffers, IO_BANKSIZE, IO_BANKMAX);

            PreviousCheckTime = StartTime = time(NULL);
            BlockStartTime                = timemsec();
            CurrentProgressPercentage = OldProgressPercentage = 0;
            for (lsn = 0; lsn < params->sectors; lsn += SectorsToRead) {
                if (InstallerThreadCommandParam != INSTALLER_CMD_NONE) {
                    switch (InstallerThreadCommandParam) {
                        case INSTALLER_CMD_ABORT:
                            MessageBox(params->ParentDialog, L"Game copying aborted.", L"Installation cancelled", MB_OK | MB_ICONINFORMATION);
                            result = -EEXTABORT;
                            goto GameCopy_end;
                            break;
                    }
                }

                SectorsToRead = params->sectors - lsn > IO_BUFFER_SIZE ? IO_BUFFER_SIZE : params->sectors - lsn;
                wrBuffer      = IOGetNextWrBuffer();

                BlockStartTime = timemsec();
                for (retries = 0; retries < RETRY_COUNT; retries++) {
                    result = HDLGManReadGame(wrBuffer, 2048 * SectorsToRead);

                    if (result == 0) { // No error.
                        result = 0;
                        break;
                    } else {
                        // Terminate and reconnect (data connection was either lost or closed by remote peer).
                        CloseDataConnection();

                        result = HDLGManGetIOStatus();

                        if (result == 0) {                    // All OK. Only the data connection was disconnected.
                        } else if (result == -EEXTCONNLOST) { // Command socket disconnected. Attempt to reconnect.
                            result = AttemptReconnectCmd();

                            if (result != 0)
                                break;
                        } else { // Some other error, like disk I/O error (non-recoverable).
                            displayAlertMsg(L"Error reading game: %d, %d, LSN: 0x%0x\n", result, GetLastError(), lsn);
                            result = -EIO;
                            break;
                        }

                        if (result == 0) { // Command connection is okay. Attempt to reconnect for data connection.
                            for (reconnects = 0; reconnects < RECONNECT_COUNT; reconnects++) {
                                if ((result = ReconnectToServerForRead(params->partition, params->sectors - lsn, lsn)) == 0) { // Successfully re-connected.
                                    break;
                                }
                            }

                            if (result != 0) {
                                result = -EEXTCONNLOST;
                                break;
                            }
                        }
                    }
                }

                if (result == -EEXTCONNLOST)
                    displayAlertMsg(L"The connection to the console has been lost.\nPlease reconnect.\n");

                if (result != 0)
                    break;

                IOSignalWriteDone(SectorsToRead * 2048);
                if (IOGetStatus() == IO_THREAD_STATE_ERROR) {
                    displayAlertMsg(L"Error writing sectors!\n");
                    result = -EIO;
                    break;
                }

                TimeElaspedBlock = diffmsec(BlockStartTime, timemsec());
                CurrentTime      = time(NULL);
                if (difftime(CurrentTime, PreviousCheckTime) > 0) {
                    if (TimeElaspedBlock > 0) {
                        speed = (unsigned int)(SectorsToRead * (2048 / 1024) * 1000 / TimeElaspedBlock);
                        swprintf(TextBuffer, sizeof(TextBuffer) / sizeof(wchar_t), L"%u KB/s", speed);
                        SetWindowText(GetDlgItem(params->ParentDialog, IDC_STATIC_CURRENT_SPEED), TextBuffer);
                    }

                    CurrentProgressPercentage = (unsigned char)(lsn * 100 / (params->sectors));
                    if (CurrentProgressPercentage != OldProgressPercentage) {
                        SendMessage(GetDlgItem(params->ParentDialog, IDC_PROGRESS_COPY), PBM_SETPOS, (WPARAM)CurrentProgressPercentage, 0);
                        OldProgressPercentage = CurrentProgressPercentage;

                        swprintf(TextBuffer, sizeof(TextBuffer) / sizeof(wchar_t), L"%u%%", CurrentProgressPercentage);
                        SetWindowText(GetDlgItem(params->ParentDialog, IDC_STATIC_PROGRESS), TextBuffer);
                    }

                    if ((TimeElasped = difftime(CurrentTime, StartTime)) > 0) {
                        if ((speed = (unsigned int)(lsn * (2048 / 1024) / TimeElasped)) > 0 && (SectorsPerSecond = speed * 1024 / 2048) > 0) {
                            swprintf(TextBuffer, sizeof(TextBuffer) / sizeof(wchar_t), L"%u KB/s", speed);
                            SetWindowText(GetDlgItem(params->ParentDialog, IDC_STATIC_AVERAGE_SPEED), TextBuffer);

                            SecondsRemaining = (params->sectors - lsn) / SectorsPerSecond;
                            HoursRemaining   = SecondsRemaining / 3600;
                            MinutesRemaining = (SecondsRemaining - HoursRemaining * 3600) / 60;
                            if (SecondsRemaining < UINT_MAX) {
                                swprintf(TextBuffer, sizeof(TextBuffer) / sizeof(wchar_t), L"%02u:%02u:%02u", HoursRemaining, MinutesRemaining, SecondsRemaining - HoursRemaining * 3600 - MinutesRemaining * 60);
                            } else {
                                wcscpy(TextBuffer, L"--:--:--");
                            }

                            SetWindowText(GetDlgItem(params->ParentDialog, IDC_STATIC_TIME_REMAINING), TextBuffer);
                        } else {
                            SetWindowText(GetDlgItem(params->ParentDialog, IDC_STATIC_AVERAGE_SPEED), L"0 bytes/s");
                            SetWindowText(GetDlgItem(params->ParentDialog, IDC_STATIC_TIME_REMAINING), L"--:--:--");
                        }
                    }

                    PreviousCheckTime = CurrentTime;
                }
            } /* End of main loop */
        GameCopy_end:
            installResult = result;

            IOEndWrite();
            closeFile(discimg);

            IOFree(&bds, (void **)&ioBuffers);
            CloseDataConnection(); // Regardless of how it went, close the data connection.

            // Do not attempt to close with a closed socket.
            if (result != -EEXTCONNLOST) {
                for (retries = 0; retries < RETRY_COUNT; retries++) {
                    result = HDLGManCloseGame();

                    if (result >= 0) { // All OK.
                        break;
                    } else if (result == -EEXTCONNLOST) { // Command socket disconnected. Attempt to reconnect.
                        result = AttemptReconnectCmd();

                        if (result != 0)
                            break;
                    } else { // Some other error, like disk I/O error (non-recoverable).
                        displayAlertMsg(L"Error closing game: %d\n", result);
                        break;
                    }
                }

                if (result == -EEXTCONNLOST)
                    displayAlertMsg(L"The connection to the console has been lost.\nPlease reconnect.\n");

                // Restore the copying result (the reason for failure).
                if (result < 0)
                    installResult = result;
            }

            // If copying did not complete successfully, delete the partially-copied image file.
            if (installResult < 0)
                _wremove(params->destination);
        } else {
            displayAlertMsg(L"Unable to create disc image file %s.\n", params->destination);
            result = -EIO;
        }
    } else {
        displayAlertMsg(L"Could not open game for copying.\n");
    }

CopyEnd:
    free(params->destination);

    if (result >= 0) {
        MessageBox(params->ParentDialog, L"Games retrieved successfully.", L"Reading complete.", MB_OK | MB_ICONINFORMATION);
        EndDialog(params->ParentDialog, TRUE);
    }

    EnableWindow(GetDlgItem(params->ParentDialog, IDCANCEL), TRUE);
    EnableWindow(GetDlgItem(params->ParentDialog, IDABORT), FALSE);
    return 0;
}

void StartInstallation(struct InstallerThreadParams *params)
{
    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&InstallerThread, params, 0, NULL);
}

void StartCopy(struct RetrieveThreadParams *params)
{
    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&RetrieveThread, params, 0, NULL);
}
