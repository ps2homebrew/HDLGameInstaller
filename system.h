enum BootDeviceIDs{
	BOOT_DEVICE_UNKNOWN = -1,
	BOOT_DEVICE_MC0 = 0,
	BOOT_DEVICE_MC1,
	BOOT_DEVICE_CDROM,
	BOOT_DEVICE_MASS,
	BOOT_DEVICE_HDD,

	BOOT_DEVICE_COUNT,
};

enum CONSOLE_REGION{
	CONSOLE_REGION_JAPAN	= 0,
	CONSOLE_REGION_USA,	//USA and Asia
	CONSOLE_REGION_EUROPE,
	CONSOLE_REGION_CHINA,

	CONSOLE_REGION_COUNT
};

/* Transfer rate modes. */
#define ATA_XFER_MODE_PIO	0x08
#define ATA_XFER_MODE_MDMA	0x20
#define ATA_XFER_MODE_UDMA	0x40

/* Parameters */
#define CDVD_INSTALL_IO_PRIORITY	0x71
#define CDVD_INSTALL_MAIN_PRIORITY	0x70

int GetBootDeviceID(void);
int GetConsoleRegion(void);
int GetConsoleVMode(void);
int GetDiscIDFromStartupFilename(const char *filename, char *DiscID);
int InitGameCDVDInformation(sceCdRMode *ReadMode, char *DiscID, char *StartupFname, unsigned char *discType, unsigned int *SectorsInDiscLayer0, unsigned int *SectorsInDiscLayer1);
int RetrieveGameInstallationSector(u32 lba, const char *partition, struct HDLGameEntry *GameEntry);
int RetrieveGameInstallationData(const char *partition, struct HDLGameEntry *GameEntry);
int RemoveGameInstallation(const char *PartPath);
int UpdateGameInstallation(const char *partition, const wchar_t *title, unsigned char CompatModeFlags, unsigned char TRType, unsigned char TRMode, unsigned char DiscType);
int CheckForExistingGameInstallation(const char *DiscID, char *ExistingPartName, unsigned int BufferSize);
void GeneratePartName(char *PartName, const char *DiscID, const wchar_t *title);
int PrepareInstallHDLGame(const wchar_t *GameTitle, const char *DiscID, const char *StartupFname, const char *TargetPartName, unsigned int SectorsInDiscLayer0, unsigned int SectorsInDiscLayer1, unsigned char DiscType, unsigned char CompatibilityModeFlags, unsigned char TRType, unsigned char TRMode);
int InstallGameFromCDVDDrive(sceCdRMode *ReadMode, const char *InstallPath, const wchar_t *title, const char *DiscID, const char *StartupFname, unsigned char DiscType, unsigned int SectorsInDiscLayer0, unsigned int SectorsInDiscLayer1, unsigned char CompatibilityModeFlags, unsigned char TRType, unsigned char TRMode);
int SysCreateThread(void *function, void *stack, unsigned int StackSize, void *arg, int priority);

int SysBootDeviceInit(void);
void sysGetFreeDiskSpaceDisplay(char *space);

#ifdef ENABLE_NETWORK_SUPPORT
int ethApplyNetIFConfig(int mode);
int ethGetNetIFLinkStatus(void);
int ethWaitValidNetIFLinkState(void);
int ethWaitValidDHCPState(void);
int ethApplyIPConfig(int use_dhcp, const struct ip4_addr *ip, const struct ip4_addr *netmask, const struct ip4_addr *gateway);
void ethInit(void);
void ethReinit(void);
void ethValidate(void);
void ethGetIPAddressDisplay(char *ip);
#endif
