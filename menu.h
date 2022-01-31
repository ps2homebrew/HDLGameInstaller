#define GAME_LIST_WIDTH	600
#define GAME_LIST_MAX_DISPLAYED_GAMES	15
#define GAME_LIST_TITLE_MAX_CHARS	36
#define GAME_LIST_TITLE_MAX_PIX		(GAME_LIST_TITLE_MAX_CHARS*FNT_CHAR_WIDTH)
#define GAME_LIST_TITLE_SCROLL_INTERVAL	10
#define GAME_LIST_TITLE_SCROLL_START_PAUSE_INTERVAL	(1*60)	//Number of frames to pause by, when the title is scrolled to the end.
#define GAME_LIST_TITLE_SCROLL_END_PAUSE_INTERVAL	(3*60)	//Number of frames to pause by, when the title is scrolled to the end.

#define PROGRESS_BAR_START_X	5
#define PROGRESS_BAR_START_Y	320
#define PROGRESS_BAR_LENGTH	600

#define SOFT_KEYBOARD_DISPLAY_X_LOCATION	70
#define SOFT_KEYBOARD_DISPLAY_Y_LOCATION	70
#define SOFT_KEYBOARD_X_LOCATION	90
#define SOFT_KEYBOARD_Y_LOCATION	140
#define SOFT_KEYBOARD_MAX_DISPLAYED_CHARS	33
#define SOFT_KEYBOARD_MAX_DISPLAYED_WIDTH	(SOFT_KEYBOARD_MAX_DISPLAYED_CHARS*(short int)(SOFT_KEYBOARD_CHAR_WIDTH))
#define SOFT_KEYBOARD_FONT_SCALE	1.0f
#define SOFT_KEYBOARD_CHAR_WIDTH	(16*SOFT_KEYBOARD_FONT_SCALE)
#define SOFT_KEYBOARD_CHAR_HEIGHT	(16*SOFT_KEYBOARD_FONT_SCALE)
#define SOFT_KEYBOARD_CURSOR_HEIGHT	36

#define MAX_DEVICES_IN_ROW	2
#define DEVICE_LIST_X		128
#define DEVICE_LIST_Y		64

#define MENU_BLANK_MAX_CHARS	38
#define MENU_BLANK_MAX_WIDTH	(MENU_BLANK_MAX_CHARS*FNT_CHAR_WIDTH)
#define MAX_BTN_LAB_LEN		16
#define BTN_FNT_CHAR_WIDTH	8

#define UPDATE_INTERVAL		300	//Update interval: 5s (5 * 60FPS = 300)
#define NETSTAT_UPDATE_INTERVAL	600	//Update interval: 10s (10 * 60FPS = 600)

void MainMenu(void);
void RedrawLoadingScreen(unsigned int frame);

void DrawInstallGameScreen(const wchar_t *GameTitle, const char *DiscID, unsigned char DiscType, float percentage, unsigned int rate, unsigned int SecondsRemaining);
int DisplaySoftKeyboard(wchar_t *buffer, unsigned int length, unsigned int options);
int ShowWaitForDiscDialog(void);
