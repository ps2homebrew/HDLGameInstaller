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
#include "iso9660.h"
#include "OSD.h"
#include "resource.h"

static HINSTANCE g_hInstance;
extern int InstallerThreadCommandParam;

enum SPACE_UNIT {
    SPACE_UNIT_KB,
    SPACE_UNIT_MB,
    SPACE_UNIT_GB,

    SPACE_UNIT_COUNT
};

static struct HDLGameEntry *g_GameList = NULL;
static int g_NumGamesInList            = 0;

static void ToggleInstallDialogSourceControls(HWND hwndDlg, int SourceIsDiscDrive);
static void InitFreeSpace(HWND hwndDlg);
static void ToggleIconSourceControls(HWND hwndDlg, int IconSource);
static INT_PTR CALLBACK InstallGameProgressDialog(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK CopyGameProgressDialog(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static int VerifyCommonGameDataFormInput(HWND hwnd);
static void PostInstallCleanup(struct GameSettings *GameSettings);
static INT_PTR CALLBACK InstallGameDlg(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK JobListDlg(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static void ToggleUpdateIconControls(HWND hwnd, int IsEnabled);
static INT_PTR CALLBACK EditGameDlg(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static void ToggleMainDialogControls(HWND hwndDlg, int state);
static void FreeGamesList(void);
static int RefreshGamesList(HWND hwnd);
static void CleanupClientConnection(void);
static void DisconnectServer(HWND hwnd);
static void InitFreeSpace(HWND hwndDlg);
static INT_PTR CALLBACK MainDlg(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

static void ToggleInstallDialogSourceControls(HWND hwndDlg, int SourceIsDiscDrive)
{
    if (SourceIsDiscDrive) {
        EnableWindow(GetDlgItem(hwndDlg, IDC_COMBO_DRIVE_SRC), TRUE);
        EnableWindow(GetDlgItem(hwndDlg, IDC_EDIT_DISC_IMG_SRC), FALSE);
        EnableWindow(GetDlgItem(hwndDlg, IDC_BUTTON_BROWSE_DISC_IMAGE), FALSE);
    } else {
        EnableWindow(GetDlgItem(hwndDlg, IDC_COMBO_DRIVE_SRC), FALSE);
        EnableWindow(GetDlgItem(hwndDlg, IDC_EDIT_DISC_IMG_SRC), TRUE);
        EnableWindow(GetDlgItem(hwndDlg, IDC_BUTTON_BROWSE_DISC_IMAGE), TRUE);
    }
}

static void ToggleIconSourceControls(HWND hwndDlg, int IconSource)
{
    switch (IconSource) {
        case 2:
            EnableWindow(GetDlgItem(hwndDlg, IDC_EDIT_ICON_EXT_PATH), TRUE);
            EnableWindow(GetDlgItem(hwndDlg, IDC_BUTTON_ICON_BROWSE), TRUE);
            break;
        default:
            EnableWindow(GetDlgItem(hwndDlg, IDC_EDIT_ICON_EXT_PATH), FALSE);
            EnableWindow(GetDlgItem(hwndDlg, IDC_BUTTON_ICON_BROWSE), FALSE);
    }
}

static INT_PTR CALLBACK InstallGameProgressDialog(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    INT_PTR result;
    static struct InstallerThreadParams InstallerThreadParams;

    result = TRUE;
    switch (uMsg) {
        case WM_CLOSE:
            break;
        case WM_INITDIALOG:
            EnableWindow(GetDlgItem(hwnd, IDCANCEL), FALSE);
            EnableWindow(GetDlgItem(hwnd, IDABORT), TRUE);

            InstallerThreadParams.jobs         = (struct JobParams *)lParam;
            InstallerThreadParams.ParentDialog = hwnd;
            InstallerThreadCommandParam        = INSTALLER_CMD_NONE;
            StartInstallation(&InstallerThreadParams);
            break;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDABORT:
                    if (MessageBox(hwnd, L"Are you sure that you want to cancel the game installation?", L"Cancel game installation?", MB_YESNO | MB_ICONQUESTION) == IDYES)
                        InstallerThreadCommandParam = INSTALLER_CMD_ABORT;
                    break;
                case IDCANCEL:
                    EndDialog(hwnd, FALSE);
                    break;
                default:
                    result = FALSE;
            }
        default:
            result = FALSE;
    }

    return result;
}

static INT_PTR CALLBACK CopyGameProgressDialog(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    INT_PTR result;
    OPENFILENAME ofn;
    wchar_t TargetPathBuffer[256];
    static struct RetrieveThreadParams RetrieveThreadParams;
    struct HDLGameEntry *game;

    result = TRUE;
    switch (uMsg) {
        case WM_CLOSE:
            break;
        case WM_INITDIALOG:
            game = (struct HDLGameEntry *)lParam;

            EnableWindow(GetDlgItem(hwnd, IDCANCEL), FALSE);
            EnableWindow(GetDlgItem(hwnd, IDABORT), TRUE);

            ofn.lStructSize       = sizeof(OPENFILENAME);
            ofn.hwndOwner         = hwnd;
            ofn.hInstance         = NULL;
            ofn.lpstrFilter       = L"ISO9660 disc images\0*.iso\0All Files\0*.*\0\0";
            ofn.lpstrCustomFilter = NULL;
            ofn.nMaxCustFilter    = 0;
            ofn.nFilterIndex      = 0;
            ofn.lpstrFile         = TargetPathBuffer;
            ofn.nMaxFile          = sizeof(TargetPathBuffer) / sizeof(wchar_t);
            ofn.lpstrFileTitle    = NULL;
            ofn.nMaxFileTitle     = 0;
            ofn.lpstrInitialDir   = NULL;
            ofn.lpstrTitle        = NULL;
            ofn.lpstrDefExt       = NULL;
            //			ofn.lCustData=NULL;
            ofn.lpfnHook        = NULL;
            ofn.lpTemplateName  = NULL;
            TargetPathBuffer[0] = '\0';
            ofn.Flags           = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            if (GetSaveFileName(&ofn)) {
                if ((RetrieveThreadParams.destination = malloc((wcslen(TargetPathBuffer) + 1) * sizeof(wchar_t))) != NULL) {
                    wcscpy(RetrieveThreadParams.destination, TargetPathBuffer);
                    strcpy(RetrieveThreadParams.partition, game->PartName);
                    RetrieveThreadParams.sectors      = game->sectors;
                    RetrieveThreadParams.ParentDialog = hwnd;
                    InstallerThreadCommandParam       = INSTALLER_CMD_NONE;
                    StartCopy(&RetrieveThreadParams);
                }
            } else {
                EndDialog(hwnd, FALSE);
                break;
            }
            break;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDABORT:
                    if (MessageBox(hwnd, L"Are you sure that you want to cancel the game copying?", L"Cancel game copying?", MB_YESNO | MB_ICONQUESTION) == IDYES)
                        InstallerThreadCommandParam = INSTALLER_CMD_ABORT;
                    break;
                case IDCANCEL:
                    EndDialog(hwnd, FALSE);
                    break;
                default:
                    result = FALSE;
            }
        default:
            result = FALSE;
    }

    return result;
}

static int VerifyCommonGameDataFormInput(HWND hwnd)
{
    int result, FieldLength;
    HWND windowItemH;

    result = 0;

    // Get the full title.
    windowItemH = GetDlgItem(hwnd, IDC_EDIT_FULL_TITLE);
    FieldLength = GetWindowTextLength(windowItemH);
    if (FieldLength < 1) {
        MessageBox(hwnd, L"Main title must be at least one character long", L"Invalid input", MB_OK | MB_ICONERROR);
        result = -EINVAL;
        goto end;
    }
    if (FieldLength > GAME_TITLE_MAX_LEN) {
        MessageBox(hwnd, L"Main title is too long (Maximum 80 characters).", L"Invalid input", MB_OK | MB_ICONERROR);
        result = -EINVAL;
        goto end;
    }

    // Get OSD title line 1.
    windowItemH = GetDlgItem(hwnd, IDC_EDIT_OSD_TITLE1);
    FieldLength = GetWindowTextLength(windowItemH);
    if (FieldLength < 1) {
        MessageBox(hwnd, L"OSD title line 1 must be at least one character long", L"Invalid input", MB_OK | MB_ICONERROR);
        result = -EINVAL;
        goto end;
    }
    if (FieldLength > OSD_TITLE_MAX_LEN) {
        MessageBox(hwnd, L"OSD title line 1 is too long (Maximum 16 characters).", L"Invalid input", MB_OK | MB_ICONERROR);
        result = -EINVAL;
        goto end;
    }

    // Get OSD title line 2.
    windowItemH = GetDlgItem(hwnd, IDC_EDIT_OSD_TITLE2);
    FieldLength = GetWindowTextLength(windowItemH);
    if (FieldLength > OSD_TITLE_MAX_LEN) {
        MessageBox(hwnd, L"OSD title line 2 is too long (Maximum 16 characters).", L"Invalid input", MB_OK | MB_ICONERROR);
        result = -EINVAL;
        goto end;
    }

    // Process the disc type option.
    if (SendMessage(GetDlgItem(hwnd, IDC_COMBO_DISC_TYPE), CB_GETCURSEL, 0, 0) == CB_ERR) {
        MessageBox(hwnd, L"Please specify the disc type.", L"Invalid input", MB_OK | MB_ICONERROR);
        result = -EINVAL;
        goto end;
    }

end:
    return result;
}

static void PostInstallCleanup(struct GameSettings *GameSettings)
{
    if (GameSettings->IconSourcePath != NULL)
        free(GameSettings->IconSourcePath);
    if (GameSettings->SourcePath != NULL)
        free(GameSettings->SourcePath);
}

static int CheckIfGameIsInstalled(HWND parentDialog, struct GameSettings *GameSettings)
{
    char DiscID[11], StartupFilename[32], partition[33];
    u32 sectorcount, dl_dvd_sectors;
    unsigned char sectortype;
    int result;
    void *discimg;

    // Begin game install process.
    discimg = openFile(GameSettings->SourcePath, O_RDONLY);

    if (discimg != NULL) {
        if ((result = GetDiscInfo(GameSettings->source, discimg, &sectorcount, &dl_dvd_sectors, &sectortype)) >= 0) {
            if (!ParsePS2CNF(discimg, DiscID, StartupFilename, NULL, sectortype)) {
                displayAlertMsg(L"Error parsing SYSTEM.CNF on disc.\n");
                return -1;
            }

            if (HDLGManGetGamePartName(DiscID, partition) == 0) {
                if (MessageBox(parentDialog, L"Game is already installed. Overwrite?", L"Game already installed", MB_YESNO | MB_ICONQUESTION) == IDNO) {
                    return -EEXIST;
                } else {
                    if ((result = HDLGManDeleteGameEntry(partition)) < 0) {
                        MessageBox(parentDialog, L"Failed to delete game.", L"Error", MB_OK | MB_ICONERROR);
                        return result;
                    }
                }
            }

            return 0;
        } else {
            displayAlertMsg(L"Unsupported disc type, or there was an error reading the disc's information.\n");
        }
    } else {
        displayAlertMsg(L"Unable to open disc image file %s.\n", GameSettings->SourcePath);
        result = -ENOENT;
    }

    return result;
}

static INT_PTR CALLBACK InstallGameDlg(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    INT_PTR result;
    OPENFILENAME ofn;
    BROWSEINFO BrowseInfo;
    static DWORD DriveList;
    static struct GameSettings *pGameSettingsOut;
    wchar_t SourcePathBuffer[256], SelectedIconFolderPath[MAX_PATH], DriveName[] = L"Z:\\";
    unsigned int i;
    HWND windowItemH;
    int FieldLength, SelectedOption, drive;
    struct GameSettings GameSettings;
    PIDLIST_ABSOLUTE SelectedIconFolderPidl;

    result = TRUE;
    switch (uMsg) {
        case WM_CLOSE:
            EndDialog(hwnd, FALSE);
            break;
        case WM_INITDIALOG:
            pGameSettingsOut = (struct GameSettings *)lParam;

            // Set up the default values and selections.
            ToggleInstallDialogSourceControls(hwnd, 1);
            CheckDlgButton(hwnd, IDC_RADIO_GAME_SOURCE_DISC, BST_CHECKED);
            CheckDlgButton(hwnd, IDC_RADIO_ICON_SRC_DEFAULT, BST_CHECKED);
            ToggleIconSourceControls(hwnd, 0);

            windowItemH = GetDlgItem(hwnd, IDC_COMBO_DRIVE_SRC);
            DriveList   = GetLogicalDrives();
            for (i = 0; i < 32; i++) {
                if (DriveList & (1 << i)) {
                    DriveName[0] = L'A' + i;
                    if (GetDriveType(DriveName) == DRIVE_CDROM) {
                        SendMessage(windowItemH, CB_ADDSTRING, 0, (LPARAM)DriveName);
                    }
                }
            }

            windowItemH = GetDlgItem(hwnd, IDC_COMBO_DISC_TYPE);
            SendMessage(windowItemH, CB_ADDSTRING, 0, (LPARAM)L"CD-ROM");
            SendMessage(windowItemH, CB_ADDSTRING, 0, (LPARAM)L"DVD");
            SendMessage(windowItemH, CB_SETCURSEL, 1, 0);
            break;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDCLOSE:
                case IDCANCEL:
                    EndDialog(hwnd, FALSE);
                    break;
                case IDOK:
                    // Validate user inputs.
                    memset(&GameSettings, 0, sizeof(GameSettings));

                    if (VerifyCommonGameDataFormInput(hwnd) == 0) {
                        // Get the full title.
                        GetWindowText(GetDlgItem(hwnd, IDC_EDIT_FULL_TITLE), GameSettings.FullTitle, GAME_TITLE_MAX_LEN + 1);

                        // Get OSD title line 1.
                        GetWindowText(GetDlgItem(hwnd, IDC_EDIT_OSD_TITLE1), GameSettings.OSDTitleLine1, OSD_TITLE_MAX_LEN + 1);

                        // Get OSD title line 2.
                        GetWindowText(GetDlgItem(hwnd, IDC_EDIT_OSD_TITLE2), GameSettings.OSDTitleLine2, OSD_TITLE_MAX_LEN + 1);

                        // Process the game compatibility options.
                        SET_COMPAT_MODE_1(GameSettings.CompatibilityModeFlags, IsDlgButtonChecked(hwnd, IDC_CHECK_COMPAT_MODE1) == BST_CHECKED);
                        SET_COMPAT_MODE_2(GameSettings.CompatibilityModeFlags, IsDlgButtonChecked(hwnd, IDC_CHECK_COMPAT_MODE2) == BST_CHECKED);
                        SET_COMPAT_MODE_3(GameSettings.CompatibilityModeFlags, IsDlgButtonChecked(hwnd, IDC_CHECK_COMPAT_MODE3) == BST_CHECKED);
                        SET_COMPAT_MODE_4(GameSettings.CompatibilityModeFlags, IsDlgButtonChecked(hwnd, IDC_CHECK_COMPAT_MODE4) == BST_CHECKED);
                        SET_COMPAT_MODE_5(GameSettings.CompatibilityModeFlags, IsDlgButtonChecked(hwnd, IDC_CHECK_COMPAT_MODE5) == BST_CHECKED);
                        SET_COMPAT_MODE_6(GameSettings.CompatibilityModeFlags, IsDlgButtonChecked(hwnd, IDC_CHECK_COMPAT_MODE6) == BST_CHECKED);
                        SET_COMPAT_MODE_7(GameSettings.CompatibilityModeFlags, IsDlgButtonChecked(hwnd, IDC_CHECK_COMPAT_MODE7) == BST_CHECKED);
                        SET_COMPAT_MODE_8(GameSettings.CompatibilityModeFlags, IsDlgButtonChecked(hwnd, IDC_CHECK_COMPAT_MODE8) == BST_CHECKED);

                        GameSettings.UseMDMA0 = IsDlgButtonChecked(hwnd, IDC_CHECK_USE_MDMA0) == BST_CHECKED;

                        // Process the user's choice of the icon source.
                        if (IsDlgButtonChecked(hwnd, IDC_RADIO_ICON_SRC_SAVE) == BST_CHECKED) {
                            GameSettings.IconSource     = 1;
                            GameSettings.IconSourcePath = NULL;
                        } else if (IsDlgButtonChecked(hwnd, IDC_RADIO_ICON_SRC_EXTERNAL) == BST_CHECKED) {
                            GameSettings.IconSource = 2;

                            windowItemH = GetDlgItem(hwnd, IDC_EDIT_ICON_EXT_PATH);
                            FieldLength = GetWindowTextLength(windowItemH);
                            if ((GameSettings.IconSourcePath = malloc(sizeof(wchar_t) * (FieldLength + 1))) != NULL) {
                                GetWindowText(windowItemH, GameSettings.IconSourcePath, FieldLength + 1);

                                if (VerifyMcSave(GameSettings.IconSourcePath) != 0) {
                                    MessageBox(hwnd, L"Invalid save folder selected.", L"Invalid input", MB_OK | MB_ICONERROR);
                                    PostInstallCleanup(&GameSettings);
                                    break;
                                }
                            } else {
                                MessageBox(hwnd, L"Internal error - can't allocate memory for icon path.", L"Internal error", MB_OK | MB_ICONERROR);
                                break;
                            }
                        } else {
                            GameSettings.IconSource     = 0;
                            GameSettings.IconSourcePath = NULL;
                        }

                        // Process the disc type option.
                        GameSettings.DiscType = SendMessage(GetDlgItem(hwnd, IDC_COMBO_DISC_TYPE), CB_GETCURSEL, 0, 0) == 0 ? SCECdPS2CD : SCECdPS2DVD;

                        if (IsDlgButtonChecked(hwnd, IDC_RADIO_GAME_SOURCE_DISC) == BST_CHECKED) {
                            GameSettings.source = 0;

                            if ((SelectedOption = SendMessage(GetDlgItem(hwnd, IDC_COMBO_DRIVE_SRC), CB_GETCURSEL, 0, 0)) != CB_ERR) {
                                for (i = 0, drive = 0; i < 32; i++) {
                                    if (DriveList & (1 << i)) {
                                        DriveName[0] = L'A' + i;
                                        if (GetDriveType(DriveName) == DRIVE_CDROM) {
                                            if (drive == SelectedOption) {
                                                break;
                                            }
                                            drive++;
                                        }
                                    }
                                }

                                if (i < 32) {
                                    GameSettings.SourcePath = (wchar_t *)malloc(sizeof(wchar_t) * 7);
                                    swprintf(GameSettings.SourcePath, 7, L"\\\\.\\%c:", 'A' + i);
                                } else {
                                    MessageBox(hwnd, L"Selected drive not found.", L"System error", MB_OK | MB_ICONERROR);
                                    PostInstallCleanup(&GameSettings);
                                    break;
                                }
                            } else {
                                MessageBox(hwnd, L"Please specify the source drive.", L"Invalid input", MB_OK | MB_ICONERROR);
                                PostInstallCleanup(&GameSettings);
                                break;
                            }
                        } else {
                            GameSettings.source = 1;

                            windowItemH = GetDlgItem(hwnd, IDC_EDIT_DISC_IMG_SRC);
                            FieldLength = GetWindowTextLength(windowItemH);
                            if ((GameSettings.SourcePath = (wchar_t *)malloc(sizeof(wchar_t) * (FieldLength + 1))) != NULL) {
                                GetWindowText(windowItemH, GameSettings.SourcePath, FieldLength + 1);
                            }
                        }

                        if (GameSettings.SourcePath != NULL) {
                            if (MessageBox(hwnd, L"Proceed?", L"Continue?", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                                if (CheckIfGameIsInstalled(hwnd, &GameSettings) == 0) {
                                    memcpy(pGameSettingsOut, &GameSettings, sizeof(struct GameSettings));
                                    EndDialog(hwnd, TRUE);
                                    break;
                                }
                            }
                        } else {
                            MessageBox(hwnd, L"Internal error - can't allocate memory for source path.", L"Internal error", MB_OK | MB_ICONERROR);
                        }
                    }

                    PostInstallCleanup(&GameSettings);
                    break;
                case IDC_BUTTON_BROWSE_DISC_IMAGE:
                    ofn.lStructSize       = sizeof(OPENFILENAME);
                    ofn.hwndOwner         = hwnd;
                    ofn.hInstance         = NULL;
                    ofn.lpstrFilter       = L"ISO9660 disc images\0*.iso\0All Files\0*.*\0\0";
                    ofn.lpstrCustomFilter = NULL;
                    ofn.nMaxCustFilter    = 0;
                    ofn.nFilterIndex      = 0;
                    ofn.lpstrFile         = SourcePathBuffer;
                    ofn.nMaxFile          = sizeof(SourcePathBuffer) / sizeof(wchar_t);
                    ofn.lpstrFileTitle    = NULL;
                    ofn.nMaxFileTitle     = 0;
                    ofn.lpstrInitialDir   = NULL;
                    ofn.lpstrTitle        = NULL;
                    ofn.lpstrDefExt       = NULL;
                    //					ofn.lCustData=NULL;
                    ofn.lpfnHook        = NULL;
                    ofn.lpTemplateName  = NULL;
                    SourcePathBuffer[0] = '\0';
                    ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    if (GetOpenFileName(&ofn)) {
                        SetDlgItemText(hwnd, IDC_EDIT_DISC_IMG_SRC, SourcePathBuffer);
                    }
                    break;
                case IDC_BUTTON_ICON_BROWSE:
                    BrowseInfo.hwndOwner      = hwnd;
                    BrowseInfo.pidlRoot       = NULL;
                    BrowseInfo.pszDisplayName = SelectedIconFolderPath;
                    BrowseInfo.lpszTitle      = L"Select Playstation 2 savedata folder";
                    BrowseInfo.ulFlags        = 0; // BIF_RETURNONLYFSDIR;
                    BrowseInfo.lpfn           = NULL;
                    if ((SelectedIconFolderPidl = SHBrowseForFolder(&BrowseInfo)) != NULL) {
                        SHGetPathFromIDList(SelectedIconFolderPidl, SelectedIconFolderPath);

                        while (VerifyMcSave(SelectedIconFolderPath) != 0) {
                            MessageBox(hwnd, L"Invalid save folder selected.", L"Invalid input", MB_OK | MB_ICONERROR);

                            if ((SelectedIconFolderPidl = SHBrowseForFolder(&BrowseInfo)) == NULL) {
                                SelectedIconFolderPath[0] = '\0';
                                break;
                            }

                            SHGetPathFromIDList(SelectedIconFolderPidl, SelectedIconFolderPath);
                        }

                        SetDlgItemText(hwnd, IDC_EDIT_ICON_EXT_PATH, SelectedIconFolderPath);
                    }
                    break;
                case IDC_RADIO_GAME_SOURCE_DISC:
                    ToggleInstallDialogSourceControls(hwnd, 1);
                    break;
                case IDC_RADIO_GAME_SOURCE_IMAGE_FILE:
                    ToggleInstallDialogSourceControls(hwnd, 0);
                    break;
                case IDC_RADIO_ICON_SRC_DEFAULT:
                    ToggleIconSourceControls(hwnd, 0);
                    break;
                case IDC_RADIO_ICON_SRC_SAVE:
                    ToggleIconSourceControls(hwnd, 1);
                    break;
                case IDC_RADIO_ICON_SRC_EXTERNAL:
                    ToggleIconSourceControls(hwnd, 2);
                    break;
                default:
                    result = FALSE;
            }
            break;
        default:
            result = FALSE;
    }

    return result;
}

static INT_PTR CALLBACK JobListDlg(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    INT_PTR result;
    static struct JobParams jobs;
    static struct GameSettings NewJob;
    int SelectedJob, JobIndex;

    result = TRUE;
    switch (uMsg) {
        case WM_CLOSE:
            for (JobIndex = 0; JobIndex < jobs.count; JobIndex++)
                PostInstallCleanup(&jobs.games[JobIndex]);

            EndDialog(hwnd, FALSE);
            break;
        case WM_INITDIALOG:
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_REMOVE), FALSE);
            EnableWindow(GetDlgItem(hwnd, IDOK), FALSE);
            memset(&jobs, 0, sizeof(struct JobParams));
            break;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDCLOSE:
                case IDCANCEL:
                    for (JobIndex = 0; JobIndex < jobs.count; JobIndex++)
                        PostInstallCleanup(&jobs.games[JobIndex]);
                    EndDialog(hwnd, FALSE);
                    break;
                case IDC_BTN_ADD:
                    if (DialogBoxParam(g_hInstance, MAKEINTRESOURCE(IDD_DIALOG_INSTALL_GAME), hwnd, &InstallGameDlg, (LPARAM)&NewJob) == TRUE) {
                        if ((jobs.games = (struct GameSettings *)realloc(jobs.games, (jobs.count + 1) * sizeof(struct GameSettings))) != NULL) {
                            memcpy(&jobs.games[jobs.count], &NewJob, sizeof(struct GameSettings));
                            SendMessage(GetDlgItem(hwnd, IDC_LB_JOBS), LB_ADDSTRING, 0, (LPARAM)NewJob.FullTitle);

                            jobs.count++;
                            if (jobs.count == 1) { // A game was just added, so enable the remove and OK buttons.
                                EnableWindow(GetDlgItem(hwnd, IDC_BTN_REMOVE), TRUE);
                                EnableWindow(GetDlgItem(hwnd, IDOK), TRUE);
                            }
                        } else {
                            jobs.count = 0;
                            EnableWindow(GetDlgItem(hwnd, IDC_BTN_REMOVE), FALSE);
                            EnableWindow(GetDlgItem(hwnd, IDOK), FALSE);
                        }
                    }
                    break;
                case IDC_BTN_REMOVE:
                    SelectedJob = SendMessage(GetDlgItem(hwnd, IDC_LB_JOBS), LB_GETCURSEL, 0, 0);
                    if (SelectedJob != LB_ERR) {
                        SendMessage(GetDlgItem(hwnd, IDC_LB_JOBS), LB_DELETESTRING, (WPARAM)SelectedJob, 0);

                        if (SelectedJob < jobs.count - 1)
                            memmove(&jobs.games[SelectedJob], &jobs.games[SelectedJob + 1], sizeof(struct GameSettings) * (jobs.count - SelectedJob - 1));

                        jobs.count--;
                        if (jobs.count < 1) {
                            free(jobs.games);
                            jobs.games = NULL;
                            EnableWindow(GetDlgItem(hwnd, IDC_BTN_REMOVE), FALSE);
                            EnableWindow(GetDlgItem(hwnd, IDOK), FALSE);
                        } else {
                            if ((jobs.games = (struct GameSettings *)realloc(jobs.games, jobs.count * sizeof(struct GameSettings))) == NULL)
                                jobs.count = 0;
                        }
                    }
                    break;
                case IDOK:
                    if (MessageBox(hwnd, L"Proceed with installation?", L"Continue?", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        if (DialogBoxParam(g_hInstance, MAKEINTRESOURCE(IDD_DIALOG_INSTALLING_GAME), hwnd, &InstallGameProgressDialog, (LPARAM)&jobs) == TRUE) {
                            for (JobIndex = 0; JobIndex < jobs.count; JobIndex++)
                                PostInstallCleanup(&jobs.games[JobIndex]);
                            EndDialog(hwnd, TRUE);
                            break;
                        }
                    }
                    break;
                default:
                    result = FALSE;
            }
            break;
        default:
            result = FALSE;
    }

    return result;
}

static void ToggleUpdateIconControls(HWND hwnd, int IsEnabled)
{
    EnableWindow(GetDlgItem(hwnd, IDC_RADIO_ICON_SRC_DEFAULT), IsEnabled);
    EnableWindow(GetDlgItem(hwnd, IDC_RADIO_ICON_SRC_SAVE), IsEnabled);
    EnableWindow(GetDlgItem(hwnd, IDC_RADIO_ICON_SRC_EXTERNAL), IsEnabled);
    EnableWindow(GetDlgItem(hwnd, IDC_EDIT_ICON_EXT_PATH), IsEnabled);
    EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_ICON_BROWSE), IsEnabled);
}

static INT_PTR CALLBACK EditGameDlg(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    INT_PTR DialogResult;
    int result;
    static struct HDLGameEntry *GameEntry;
    wchar_t SelectedIconFolderPath[MAX_PATH];
    struct ConvertedMcIcon HDDIcon;
    struct OSD_Titles OSDTitles;
    int FieldLength;
    HWND WindowItemH;
    BROWSEINFO BrowseInfo;
    PIDLIST_ABSOLUTE SelectedIconFolderPidl;
    struct GameSettings GameSettings;

    DialogResult = TRUE;
    switch (uMsg) {
        case WM_CLOSE:
            break;
        case WM_INITDIALOG:
            // Setup default options:
            CheckDlgButton(hwnd, IDC_CHECK_UPDATE_ICON, BST_UNCHECKED);
            ToggleUpdateIconControls(hwnd, FALSE);

            // Read the game's data and fill in the fields.
            GameEntry = (struct HDLGameEntry *)lParam;
            wcsncpy(GameSettings.FullTitle, GameEntry->GameTitle, GAME_TITLE_MAX_LEN);
            GameSettings.FullTitle[GAME_TITLE_MAX_LEN] = '\0';
            SetWindowText(GetDlgItem(hwnd, IDC_EDIT_FULL_TITLE), GameSettings.FullTitle);

            // Read the game's OSD titles and fill in the fields.
            if (HDLGManGetGameInstallationOSDTitles((char *)lParam, &OSDTitles) < 0) {
                MessageBox(hwnd, L"Failed to read game's OSD titles.", L"Error", MB_OK | MB_ICONWARNING);
            } else {
                wcscpy(GameSettings.OSDTitleLine1, OSDTitles.title1);
                SetWindowText(GetDlgItem(hwnd, IDC_EDIT_OSD_TITLE1), GameSettings.OSDTitleLine1);
                wcscpy(GameSettings.OSDTitleLine2, OSDTitles.title2);
                SetWindowText(GetDlgItem(hwnd, IDC_EDIT_OSD_TITLE2), GameSettings.OSDTitleLine2);
            }

            WindowItemH = GetDlgItem(hwnd, IDC_COMBO_DISC_TYPE);
            SendMessage(WindowItemH, CB_ADDSTRING, 0, (LPARAM)L"CD-ROM");
            SendMessage(WindowItemH, CB_ADDSTRING, 0, (LPARAM)L"DVD");
            SendMessage(WindowItemH, CB_SETCURSEL, GameEntry->DiscType == SCECdPS2DVD ? 1 : 0, 0);

            CheckDlgButton(hwnd, IDC_CHECK_COMPAT_MODE1, GET_COMPAT_MODE_1(GameEntry->CompatibilityModeFlags) ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_CHECK_COMPAT_MODE2, GET_COMPAT_MODE_2(GameEntry->CompatibilityModeFlags) ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_CHECK_COMPAT_MODE3, GET_COMPAT_MODE_3(GameEntry->CompatibilityModeFlags) ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_CHECK_COMPAT_MODE4, GET_COMPAT_MODE_4(GameEntry->CompatibilityModeFlags) ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_CHECK_COMPAT_MODE5, GET_COMPAT_MODE_5(GameEntry->CompatibilityModeFlags) ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_CHECK_COMPAT_MODE6, GET_COMPAT_MODE_6(GameEntry->CompatibilityModeFlags) ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_CHECK_COMPAT_MODE7, GET_COMPAT_MODE_7(GameEntry->CompatibilityModeFlags) ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_CHECK_COMPAT_MODE8, GET_COMPAT_MODE_8(GameEntry->CompatibilityModeFlags) ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_CHECK_USE_MDMA0, (GameEntry->TRType == ATA_XFER_MODE_MDMA && GameEntry->TRMode == 0) ? BST_CHECKED : BST_UNCHECKED);
            break;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDCANCEL:
                    EndDialog(hwnd, FALSE);
                    break;
                case IDOK:
                    if (VerifyCommonGameDataFormInput(hwnd) == 0) {
                        // Get the full title.
                        GetWindowText(GetDlgItem(hwnd, IDC_EDIT_FULL_TITLE), GameSettings.FullTitle, GAME_TITLE_MAX_LEN + 1);
                        wcscpy(GameEntry->GameTitle, GameSettings.FullTitle);

                        // Get OSD title line 1.
                        GetWindowText(GetDlgItem(hwnd, IDC_EDIT_OSD_TITLE1), GameSettings.OSDTitleLine1, OSD_TITLE_MAX_LEN + 1);

                        // Get OSD title line 2.
                        GetWindowText(GetDlgItem(hwnd, IDC_EDIT_OSD_TITLE2), GameSettings.OSDTitleLine2, OSD_TITLE_MAX_LEN + 1);

                        // Process the game compatibility options.
                        SET_COMPAT_MODE_1(GameEntry->CompatibilityModeFlags, IsDlgButtonChecked(hwnd, IDC_CHECK_COMPAT_MODE1) == BST_CHECKED);
                        SET_COMPAT_MODE_2(GameEntry->CompatibilityModeFlags, IsDlgButtonChecked(hwnd, IDC_CHECK_COMPAT_MODE2) == BST_CHECKED);
                        SET_COMPAT_MODE_3(GameEntry->CompatibilityModeFlags, IsDlgButtonChecked(hwnd, IDC_CHECK_COMPAT_MODE3) == BST_CHECKED);
                        SET_COMPAT_MODE_4(GameEntry->CompatibilityModeFlags, IsDlgButtonChecked(hwnd, IDC_CHECK_COMPAT_MODE4) == BST_CHECKED);
                        SET_COMPAT_MODE_5(GameEntry->CompatibilityModeFlags, IsDlgButtonChecked(hwnd, IDC_CHECK_COMPAT_MODE5) == BST_CHECKED);
                        SET_COMPAT_MODE_6(GameEntry->CompatibilityModeFlags, IsDlgButtonChecked(hwnd, IDC_CHECK_COMPAT_MODE6) == BST_CHECKED);
                        SET_COMPAT_MODE_7(GameEntry->CompatibilityModeFlags, IsDlgButtonChecked(hwnd, IDC_CHECK_COMPAT_MODE7) == BST_CHECKED);
                        SET_COMPAT_MODE_8(GameEntry->CompatibilityModeFlags, IsDlgButtonChecked(hwnd, IDC_CHECK_COMPAT_MODE8) == BST_CHECKED);

                        if (IsDlgButtonChecked(hwnd, IDC_CHECK_USE_MDMA0) == BST_CHECKED) {
                            GameEntry->TRType = ATA_XFER_MODE_MDMA;
                            GameEntry->TRMode = 0;
                        } else {
                            GameEntry->TRType = ATA_XFER_MODE_UDMA;
                            GameEntry->TRMode = 4;
                        }

                        // Process the disc type option.
                        GameEntry->DiscType = SendMessage(GetDlgItem(hwnd, IDC_COMBO_DISC_TYPE), CB_GETCURSEL, 0, 0) == 0 ? SCECdPS2CD : SCECdPS2DVD;

                        if (HDLGManUpdateGameEntry(GameEntry) < 0) {
                            MessageBox(hwnd, L"Failed to update game entry.", L"Error", MB_OK | MB_ICONERROR);
                            break;
                        }

                        // Update the OSD resources.
                        memset(&HDDIcon, 0, sizeof(HDDIcon));
                        if (IsDlgButtonChecked(hwnd, IDC_CHECK_UPDATE_ICON) != BST_CHECKED) {
                            if (UpdateGameInstallationOSDResources(GameEntry->PartName, GameSettings.OSDTitleLine1, GameSettings.OSDTitleLine2) != 0) {
                                MessageBox(hwnd, L"Failed to update OSD resources.", L"Error", MB_OK | MB_ICONERROR);
                            } else {
                                MessageBox(hwnd, L"Game entry updated successfully!.", L"Update completed!", MB_OK | MB_ICONINFORMATION);
                                EndDialog(hwnd, TRUE);
                                break;
                            }
                        } else {
                            // Load the new icon and set up the title.
                            // Process the user's choice of the icon source.
                            if (IsDlgButtonChecked(hwnd, IDC_RADIO_ICON_SRC_SAVE) == BST_CHECKED) {
                                GameSettings.IconSource = 1;
                            } else if (IsDlgButtonChecked(hwnd, IDC_RADIO_ICON_SRC_EXTERNAL) == BST_CHECKED) {
                                GameSettings.IconSource = 2;

                                WindowItemH = GetDlgItem(hwnd, IDC_EDIT_ICON_EXT_PATH);
                                FieldLength = GetWindowTextLength(WindowItemH);
                                if ((GameSettings.IconSourcePath = (wchar_t *)malloc(sizeof(wchar_t) * (FieldLength + 1))) != NULL) {
                                    GetWindowText(WindowItemH, GameSettings.IconSourcePath, FieldLength + 1);

                                    if (VerifyMcSave(GameSettings.IconSourcePath) != 0) {
                                        MessageBox(hwnd, L"Invalid save folder selected.", L"Invalid savedata", MB_OK | MB_ICONERROR);
                                        break;
                                    }
                                } else {
                                    MessageBox(hwnd, L"Internal error - can't allocate memory for icon path.", L"Internal error", MB_OK | MB_ICONERROR);
                                    FreeConvertedMcSave(&HDDIcon);
                                    break;
                                }
                            } else {
                                GameSettings.IconSource = 0;
                            }

                            if (GameSettings.IconSource == 2) {
                                if (ConvertMcSave(GameSettings.IconSourcePath, &HDDIcon, GameSettings.OSDTitleLine1, GameSettings.OSDTitleLine2) != 0) {
                                    MessageBox(hwnd, L"Failed to load icon from savedata. The default icon will be used.", L"Failed to load icon", MB_OK | MB_ICONWARNING);
                                    GameSettings.IconSource = 0;
                                    memset(&HDDIcon, 0, sizeof(HDDIcon));
                                }
                            } else
                                memset(&HDDIcon, 0, sizeof(HDDIcon));

                        RetryIconInstall:
                            if ((result = InstallGameInstallationOSDResources(GameEntry->PartName, GameEntry->DiscID, &GameSettings, &HDDIcon)) >= 0) {
                                if (result != 1 && (GameSettings.IconSource == 1 || GameSettings.IconSource == 2)) {
                                    MessageBox(hwnd, L"Failed to load icon from savedata. The default icon will be used.", L"Failed to load icon", MB_OK | MB_ICONWARNING);
                                    GameSettings.IconSource = 0;
                                    goto RetryIconInstall;
                                }
                            } else {
                                displayAlertMsg(L"Error writing OSD resources: %d\n", result);
                            }

                            FreeConvertedMcSave(&HDDIcon);

                            if (result >= 0) {
                                MessageBox(hwnd, L"Game entry updated successfully!.", L"Update completed!", MB_OK | MB_ICONINFORMATION);
                                EndDialog(hwnd, TRUE);
                                break;
                            } else {
                                MessageBox(hwnd, L"Failed to update game entry.", L"Error", MB_OK | MB_ICONERROR);
                            }
                        }
                    }
                    break;
                case IDC_CHECK_UPDATE_ICON:
                    ToggleUpdateIconControls(hwnd, IsDlgButtonChecked(hwnd, IDC_CHECK_UPDATE_ICON) == BST_CHECKED);
                    CheckDlgButton(hwnd, IDC_RADIO_ICON_SRC_DEFAULT, BST_CHECKED);
                    ToggleIconSourceControls(hwnd, 0);
                    break;
                case IDC_RADIO_ICON_SRC_DEFAULT:
                    ToggleIconSourceControls(hwnd, 0);
                    break;
                case IDC_RADIO_ICON_SRC_SAVE:
                    ToggleIconSourceControls(hwnd, 1);
                    break;
                case IDC_RADIO_ICON_SRC_EXTERNAL:
                    ToggleIconSourceControls(hwnd, 2);
                    break;
                case IDC_BUTTON_ICON_BROWSE:
                    BrowseInfo.hwndOwner      = hwnd;
                    BrowseInfo.pidlRoot       = NULL;
                    BrowseInfo.pszDisplayName = SelectedIconFolderPath;
                    BrowseInfo.lpszTitle      = L"Select Playstation 2 savedata folder";
                    BrowseInfo.ulFlags        = 0; // BIF_RETURNONLYFSDIR;
                    BrowseInfo.lpfn           = NULL;
                    if ((SelectedIconFolderPidl = SHBrowseForFolder(&BrowseInfo)) != NULL) {
                        SHGetPathFromIDList(SelectedIconFolderPidl, SelectedIconFolderPath);

                        while (VerifyMcSave(SelectedIconFolderPath) != 0) {
                            MessageBox(hwnd, L"Invalid save folder selected.", L"Invalid input", MB_OK | MB_ICONERROR);

                            if ((SelectedIconFolderPidl = SHBrowseForFolder(&BrowseInfo)) == NULL) {
                                SelectedIconFolderPath[0] = '\0';
                                break;
                            }

                            SHGetPathFromIDList(SelectedIconFolderPidl, SelectedIconFolderPath);
                        }

                        SetDlgItemText(hwnd, IDC_EDIT_ICON_EXT_PATH, SelectedIconFolderPath);
                    }
                    break;
                default:
                    DialogResult = FALSE;
            }
        default:
            DialogResult = FALSE;
    }

    return DialogResult;
}

static void ToggleMainDialogControls(HWND hwndDlg, int state)
{
    EnableWindow(GetDlgItem(hwndDlg, IDC_GAME_LIST), state);
    EnableWindow(GetDlgItem(hwndDlg, IDC_BTN_EDIT_GAME), state);
    EnableWindow(GetDlgItem(hwndDlg, IDC_BTN_DELETE_GAME), state);
    EnableWindow(GetDlgItem(hwndDlg, IDC_BTN_COPY_GAME), state);
    EnableWindow(GetDlgItem(hwndDlg, IDC_BTN_INSTALL_GAME), state);
    EnableWindow(GetDlgItem(hwndDlg, IDC_BUTTON_POWEROFF), state);
}

static void FreeGamesList(void)
{
    if (g_GameList != NULL) {
        free(g_GameList);
        g_GameList       = NULL;
        g_NumGamesInList = 0;
    }
}

static int RefreshGamesList(HWND hwnd)
{
    int NumGamesInList, i;
    HWND GameListH;

    FreeGamesList();
    GameListH = GetDlgItem(hwnd, IDC_GAME_LIST);
    SendMessage(GameListH, LB_RESETCONTENT, 0, 0);
    if ((NumGamesInList = HDLGManLoadGameList(&g_GameList)) >= 0) {
        for (i = 0; i < NumGamesInList; i++) {
            SendMessage(GameListH, LB_SETITEMDATA, (int)SendMessage(GameListH, LB_ADDSTRING, 0, (LPARAM)g_GameList[i].GameTitle), (LPARAM)i);
        }

        g_NumGamesInList = NumGamesInList;
    }

    InitFreeSpace(hwnd);

    return NumGamesInList;
}

static void CleanupClientConnection(void)
{
    FreeGamesList();
    DisconnectSocketConnection();
}

static void DisconnectServer(HWND hwnd)
{
    CleanupClientConnection();
    SendMessage(GetDlgItem(hwnd, IDC_GAME_LIST), LB_RESETCONTENT, 0, 0);
    ToggleMainDialogControls(hwnd, FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_IPADDRESS_SRV), TRUE);
    SetWindowText(GetDlgItem(hwnd, IDC_BTN_CONNECT), L"Connect");
}

static void InitFreeSpace(HWND hwndDlg)
{
    unsigned long int FreeSpace;
    wchar_t *labels[SPACE_UNIT_COUNT] = {
        L"KB",
        L"MB",
        L"GB"};
    int unit, i;
    wchar_t space[16];

    FreeSpace = HDLGManGetFreeSpace() / 2; // 1KB = 2x512-byte sectors
    for (i = 0, unit = 0; i < SPACE_UNIT_COUNT - 1; i++, unit++) {
        if (FreeSpace > 1000) {
            FreeSpace /= 1024;
        } else {
            break;
        }
    }
    swprintf(space, sizeof(space) / sizeof(wchar_t), L"%lu %s", FreeSpace, labels[unit]);

    SetWindowText(GetDlgItem(hwndDlg, IDC_FREE_SPACE), space);
}

static INT_PTR CALLBACK MainDlg(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    INT_PTR result;
    DWORD ServerIPAddress;
    char IPAddressString[16]; // Format: XXX.XXX.XXX.XXX
    int SelectedOption, rcode;
    static unsigned char IsConnected = FALSE;
    HWND hwndGameList;

    result = TRUE;
    switch (uMsg) {
        case WM_CLOSE:
            CleanupClientConnection();
            EndDialog(hwndDlg, TRUE);
            break;
        case WM_INITDIALOG:
            ToggleMainDialogControls(hwndDlg, FALSE);
            break;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDCLOSE:
                    CleanupClientConnection();
                    EndDialog(hwndDlg, TRUE);
                    break;
                case IDC_BTN_DELETE_GAME:
                    hwndGameList   = GetDlgItem(hwndDlg, IDC_GAME_LIST);
                    SelectedOption = SendMessage(hwndGameList, LB_GETCURSEL, 0, 0);
                    if (SelectedOption != LB_ERR) {
                        if (MessageBox(hwndDlg, L"Are you sure that you want to delete the selected game?", L"Delete game?", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                            if (HDLGManDeleteGameEntry(g_GameList[SelectedOption].PartName) < 0) {
                                MessageBox(hwndDlg, L"Failed to delete game.", L"Error", MB_OK | MB_ICONERROR);
                            } else {
                                SendMessage(hwndGameList, LB_DELETESTRING, (WPARAM)SelectedOption, 0);
                                MessageBox(hwndDlg, L"Game deleted successfully.", L"Game deleted", MB_OK | MB_ICONINFORMATION);
                            }
                        }
                    }
                    break;
                case IDC_BTN_COPY_GAME:
                    if ((SelectedOption = SendMessage(GetDlgItem(hwndDlg, IDC_GAME_LIST), LB_GETCURSEL, 0, 0)) != LB_ERR) {
                        DialogBoxParam(g_hInstance, MAKEINTRESOURCE(IDD_DIALOG_COPYING_GAME), hwndDlg, &CopyGameProgressDialog, (LPARAM)&g_GameList[SelectedOption]);
                    }
                    break;
                case IDC_BTN_INSTALL_GAME:
                    if (DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_DIALOG_JOB_LIST), hwndDlg, &JobListDlg) == TRUE) {
                        RefreshGamesList(hwndDlg);
                    }
                    break;
                case IDC_BTN_EDIT_GAME:
                    hwndGameList = GetDlgItem(hwndDlg, IDC_GAME_LIST);
                    if ((SelectedOption = SendMessage(hwndGameList, LB_GETCURSEL, 0, 0)) != LB_ERR) {
                        if (DialogBoxParam(g_hInstance, MAKEINTRESOURCE(IDD_DIALOG_EDIT_GAME), hwndDlg, &EditGameDlg, (LPARAM)&g_GameList[SelectedOption]) == TRUE) {
                            SendMessage(hwndGameList, LB_DELETESTRING, (WPARAM)SelectedOption, 0);
                            SendMessage(hwndGameList, LB_INSERTSTRING, (WPARAM)SelectedOption, (LPARAM)g_GameList[SelectedOption].GameTitle);
                        }
                    }
                    break;
                case IDC_BTN_CONNECT:
                    if (!IsConnected) {
                        SendMessage(GetDlgItem(hwndDlg, IDC_IPADDRESS_SRV), IPM_GETADDRESS, 0, (LPARAM)&ServerIPAddress);
                        sprintf(IPAddressString, "%u.%u.%u.%u", ServerIPAddress >> 24 & 0xFF, ServerIPAddress >> 16 & 0xFF, ServerIPAddress >> 8 & 0xFF, ServerIPAddress & 0xFF);
#ifndef UI_TEST_MODE
                        if (ConnectToServer(IPAddressString) == 0) {
                            if ((result = ExchangeHandshakesWithServer()) == 0) {
#endif
                                RefreshGamesList(hwndDlg);
                                EnableWindow(GetDlgItem(hwndDlg, IDC_IPADDRESS_SRV), FALSE);
                                SetWindowText(GetDlgItem(hwndDlg, IDC_BTN_CONNECT), L"Disconnect");
                                ToggleMainDialogControls(hwndDlg, TRUE);
                                IsConnected = TRUE;
#ifndef UI_TEST_MODE
                            } else {
                                if (result == -EEXTCONNLOST) {
                                    MessageBox(hwndDlg, L"The connection to the console has been lost.\nPlease reconnect.\n", L"Error", MB_OK | MB_ICONERROR);
                                } else {
                                    MessageBox(hwndDlg, L"Server version is incompatible with this client.", L"Connection failed", MB_OK | MB_ICONERROR);
                                }
                            }
                        } else {
                            MessageBox(hwndDlg, L"Unable to connect to the server.", L"Connection failed", MB_OK | MB_ICONERROR);
                        }
#endif
                    } else {
                        DisconnectServer(hwndDlg);
                        IsConnected = FALSE;
                    }
                    break;
                case IDC_BUTTON_POWEROFF:
                    rcode = HDLGManPowerOffServer();

                    if (rcode < 0) {
                        if (rcode == -EBUSY)
                            MessageBox(hwndDlg, L"There are other users still connected to the server.", L"Error", MB_OK | MB_ICONERROR);
                        else
                            MessageBox(hwndDlg, L"Failed to power off server.", L"Error", MB_OK | MB_ICONERROR);
                    } else {
                        DisconnectServer(hwndDlg);
                        IsConnected = FALSE;
                    }
                    break;
                default:
                    result = FALSE;
            }
            break;
        default:
            result = FALSE;
    }

    return result;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    wchar_t TextBuffer[64];

    InitializeClientSys();

    g_hInstance = hInstance;
    if (DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG_MAIN), NULL, &MainDlg) < 0) {
        swprintf(TextBuffer, sizeof(TextBuffer) / sizeof(wchar_t), L"Unable to create main dialog box. Code: 0x%08x", GetLastError());
        MessageBox(NULL, TextBuffer, L"Error", MB_OK | MB_ICONERROR);
    }

    DeinitializeClientSys();

    return 0;
}
