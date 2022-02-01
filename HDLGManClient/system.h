typedef signed char s8;
typedef unsigned char u8;
typedef signed short int s16;
typedef unsigned short int u16;
typedef signed int s32;
typedef unsigned int u32;
typedef signed long long int s64;
typedef unsigned long long int u64;

int SystemDriveGetDiscInfo(void *driveH, u32 *nSectors, u8 *discType);
void *openFile(wchar_t *path, int mode);
void closeFile(void *handle);
int readFile(void *handle, void *buffer, int nbytes);
int writeFile(void *handle, void *buffer, int nbytes);
s64 seekFile(void *handle, s64 offset, int origin);
unsigned long int timemsec(void);
unsigned long int diffmsec(unsigned long int d1, unsigned long int d2);

void displayAlertMsg(const wchar_t *message, ...);

#ifndef O_RDONLY
	#define O_RDONLY	1
	#define O_WRONLY	2
	#define O_RDWR		3

	#define O_CREAT		00100
	#define O_TRUNC		01000

	#define O_APPEND	02000    /* set append mode */
#endif
