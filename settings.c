#include <errno.h>
#include <ctype.h>
#include <iopcontrol.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <kernel.h>
#include <libcdvd.h>
#include <libmc.h>
#include <libpad.h>
#include <loadfile.h>
#include <malloc.h>
#include <netman.h>
#include <osd_config.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>
#include <hdd-ioctl.h>
#include <fileXio_rpc.h>
#include <sys/fcntl.h>

#include <gsKit.h>

#include "ipconfig.h"
#include "main.h"
#include "settings.h"

extern struct RuntimeData RuntimeData;

extern unsigned char ICON_ico[];
extern unsigned int size_ICON_ico;

static int ParseNetAddr(const char *value, u8 *addr);
static int ParseLine(const char *key, const char *value);
static int ParseSettings(char *buffer, int len);
static int CheckSpace(int space);
static int WriteSaveIcon(void);
static int WriteSaveIconSys(void);
static const char *WriteBoolean(int value);
static int WriteSettings(void);

void LoadDefaults(void)
{
	RuntimeData.EthernetLinkMode = NETMAN_NETIF_ETH_LINK_MODE_AUTO;
	RuntimeData.EthernetFlowControl = 1;
	RuntimeData.UseDHCP = 1;
	RuntimeData.SortTitles = 0;
	RuntimeData.AdvancedNetworkSettings = 0;
	RuntimeData.ip_address[0] = 192;
	RuntimeData.ip_address[1] = 168;
	RuntimeData.ip_address[2] = 0;
	RuntimeData.ip_address[3] = 10;
	RuntimeData.subnet_mask[0] = 255;
	RuntimeData.subnet_mask[1] = 255;
	RuntimeData.subnet_mask[2] = 255;
	RuntimeData.subnet_mask[3] = 0;
	RuntimeData.gateway[0] = 192;
	RuntimeData.gateway[1] = 168;
	RuntimeData.gateway[2] = 0;
	RuntimeData.gateway[3] = 1;
}

int ImportIPConfigDat(void)
{
	char ip_address_str[16], subnet_mask_str[16], gateway_str[16];

	if(ParseConfig("mc0:/SYS-CONF/IPCONFIG.DAT", ip_address_str, subnet_mask_str, gateway_str)!=0)
	{
		if(ParseConfig("mc1:/SYS-CONF/IPCONFIG.DAT", ip_address_str, subnet_mask_str, gateway_str)!=0)
		{
			return -ENOENT;
		}
	}

	RuntimeData.UseDHCP = 0;
	ParseNetAddr(ip_address_str, RuntimeData.ip_address);
	ParseNetAddr(subnet_mask_str, RuntimeData.subnet_mask);
	ParseNetAddr(gateway_str, RuntimeData.gateway);

	return 0;
}

static int ParseNetAddr(const char *value, u8 *addr)
{
	int result;
	u8 o1, o2, o3, o4;
	char *next;

	result = -EINVAL;
	o1 = (unsigned char)strtoul(value, &next, 10);
	if(next != NULL && *next == '.')
	{
		o2 = (unsigned char)strtoul(next + 1, &next, 10);
		if(next != NULL && *next == '.')
		{
			o3 = (unsigned char)strtoul(next + 1, &next, 10);
			if(next != NULL && *next == '.')
			{
				o4 = (unsigned char)strtoul(next + 1, NULL, 10);
				result = 0;

				addr[0] = o1;
				addr[1] = o2;
				addr[2] = o3;
				addr[3] = o4;
			}
		}
	}

	if(result != 0)
		printf("Settings: error - not IP address: %s\n", value);

	return result;
}

static int ParseLine(const char *key, const char *value)
{
	if(strcmp(key, "SORT") == 0)
	{
		if(strcmp(value, "true") == 0)
		{
			RuntimeData.SortTitles = 1;
			return 0;
		}
		else if(strcmp(value, "false") == 0)
		{
			RuntimeData.SortTitles = 0;
			return 0;
		}
		else
			return -EINVAL;
	}
	else if(strcmp(key, "DHCP") == 0)
	{
		if(strcmp(value, "true") == 0)
		{
			RuntimeData.UseDHCP = 1;
			return 0;
		}
		else if(strcmp(value, "false") == 0)
		{
			RuntimeData.UseDHCP = 0;
			return 0;
		}
		else
			return -EINVAL;
	}
	else if(strcmp(key, "IP") == 0)
	{
		if(ParseNetAddr(value, RuntimeData.ip_address))
			return -EINVAL;
	}
	else if(strcmp(key, "NM") == 0)
	{
		if(ParseNetAddr(value, RuntimeData.subnet_mask))
			return -EINVAL;
	}
	else if(strcmp(key, "GW") == 0)
	{
		if(ParseNetAddr(value, RuntimeData.gateway))
			return -EINVAL;
	}
	else if(strcmp(key, "ADVNETWORK") == 0)
	{
		if(strcmp(value, "true") == 0)
		{
			RuntimeData.AdvancedNetworkSettings = 1;
			return 0;
		}
		else if(strcmp(value, "false") == 0)
		{
			RuntimeData.AdvancedNetworkSettings = 0;
			return 0;
		}
		else
			return -EINVAL;
	}
	else if(strcmp(key, "LINKMODE") == 0)
	{
		RuntimeData.EthernetLinkMode = (short int)strtol(value, NULL, 0);
	}
	else if(strcmp(key, "FLOWCTRL") == 0)
	{
		if(strcmp(value, "true") == 0)
		{
			RuntimeData.EthernetFlowControl = 1;
			return 0;
		}
		else if(strcmp(value, "false") == 0)
		{
			RuntimeData.EthernetFlowControl = 0;
			return 0;
		}
		else
			return -EINVAL;
	}
	else
	{
		printf("Settings: error - unrecognized key: %s\n", key);
		return -EINVAL;
	}

	return 0;
}

static int ParseSettings(char *buffer, int len)
{
	int result;
	char *line, *pChar, *key, *pKeyEnd, *value;

	if((line = strtok(buffer, "\r\n")) != NULL)
	{
		do{
			pChar = line;

			while(isspace(*pChar))
				++pChar;

			key = pChar;	//Record key position.

			//Cross the key
			while(isalnum(*pChar))
				++pChar;
			pKeyEnd = pChar;

			while(isspace(*pChar))
				++pChar;

			//Check for the '='
			if(*pChar != '=')
			{
				printf("Settings: error - '=' missing!\n");
				return -1;
			}
			++pChar;

			*pKeyEnd = '\0';	//Replace the first character after the key with a null.

			while(isspace(*pChar))
				++pChar;

			value = pChar;	//Record value position.

			//Cross the value
			while(isgraph(*pChar))
				++pChar;
			*pChar = '\0';

			result = ParseLine(key, value);
			if(result != 0)
				break;
		}while((line = strtok(NULL, "\r\n")) != NULL);
	}

	return result;
}

int LoadSettings(void)
{
	char *buffer;
	int fd, len, result;

	LoadDefaults();

	if((result=fileXioMount("pfs1:", "hdd0:__common", FIO_MT_RDONLY)) >= 0)
	{
		if((fd = fileXioOpen(TITLE_SAVE_FOLDER_FULL"/settings.ini", O_RDONLY)) >= 0)
		{
			len = fileXioLseek(fd, 0, SEEK_END);
			fileXioLseek(fd, 0, SEEK_SET);

			if(len > 0)
			{
				if((buffer = memalign(64, len+1)) != NULL)
				{
					result = (fileXioRead(fd, buffer, len) == len) ? 0 : -EIO;
					buffer[len] = '\0';
					fileXioClose(fd);

					if(result == 0)
						result = ParseSettings(buffer, len + 1);

					free(buffer);
				}
				else
				{
					fileXioClose(fd);
					result = -ENOMEM;
				}
			}
			else
				result = -1;
		} else
			result = fd;

		fileXioUmount("pfs1:");
	}

	return result;
}

static int CheckSpace(int space)
{
	u32 zfree, zones;
	s32 zsize;

	zsize = fileXioDevctl("pfs1:", PDIOC_ZONESZ, NULL, 0, NULL, 0);
	zones = space / zsize;
	if (space % zsize > 0)
		zones++;

	zfree = (u32)fileXioDevctl("pfs1:", PDIOC_ZONEFREE, NULL, 0, NULL, 0);

	return(zfree >= zones);
}

static int WriteSaveIcon(void)
{
	iox_stat_t stat;
	int fd, result;

	if((fd = fileXioOpen(TITLE_SAVE_FOLDER_FULL"/icon.ico", O_RDONLY, TITLE_SAVE_FILE_MODE)) < 0)
	{
		if((fd = fileXioOpen(TITLE_SAVE_FOLDER_FULL"/icon.ico", O_WRONLY | O_CREAT, TITLE_SAVE_FILE_MODE)) >= 0)
		{
			result = fileXioWrite(fd, ICON_ico, size_ICON_ico) == size_ICON_ico ? 0 : -EIO;
			fileXioClose(fd);

			if(result == 0)
			{
				stat.attr = TITLE_SAVE_FILE_ATTR;
				fileXioChStat(TITLE_SAVE_FOLDER_FULL"/icon.ico", &stat, FIO_CST_ATTR);
			}
		} else
			result = fd;
	}
	else
	{	//Already exists.
		fileXioClose(fd);
		result = 0;
	}

	return result;
}

static int WriteSaveIconSys(void)
{
	iox_stat_t stat;
	int fd, result;
	static const mcIcon icon_sys = {
		"PS2D",
		MCICON_TYPE_SAVED_DATA,
		18*2,	//Newline position
		0,
		0x60,
		{	//Background colour
			{  68,  23, 116,  0 }, // Top left
			{ 255, 255, 255,  0 }, // Top right
			{ 255, 255, 255,  0 }, // Bottom left
			{  68,  23, 116,  0 }, // Bottom right
		},
		{	//Light source direction
			{ 0.5, 0.5, 0.5, 0.0 },
			{ 0.0,-0.4,-0.1, 0.0 },
			{-0.5,-0.5, 0.5, 0.0 },
		},
		{	//Light colour
			{ 0.3, 0.3, 0.3, 0.00 },
			{ 0.4, 0.4, 0.4, 0.00 },
			{ 0.5, 0.5, 0.5, 0.00 },
		},
		{ 0.50, 0.50, 0.50, 0.00 },	//Ambient light
		//  H       D       L       G       a      m        e        I
		{ 0x6782, 0x6382, 0x6b82, 0x6682, 0x8182, 0x8d82, 0x8582, 0x6882,
		//  n        s       t       a       l       l      e        r
		  0x8e82, 0x9382, 0x9482, 0x8182, 0x8c82, 0x8c82, 0x8582, 0x9282,
		//  S        e       t       t       i       n       g       s
		  0x7282, 0x8582, 0x9482, 0x9482, 0x8982, 0x8e82, 0x8782, 0x9382, 0x0000 },	//"HDLGameInstallerSettings"
		"icon.ico",
		"icon.ico",
		"icon.ico",
	};

	if((fd = fileXioOpen(TITLE_SAVE_FOLDER_FULL"/icon.sys", O_RDONLY, TITLE_SAVE_FILE_MODE)) < 0)
	{
		if((fd = fileXioOpen(TITLE_SAVE_FOLDER_FULL"/icon.sys", O_WRONLY | O_CREAT, TITLE_SAVE_FILE_MODE)) >= 0)
		{
			result = fileXioWrite(fd, &icon_sys, sizeof(icon_sys)) == sizeof(icon_sys) ? 0 : -EIO;
			fileXioClose(fd);

			if(result == 0)
			{
				stat.attr = TITLE_SAVE_FILE_ATTR;
				fileXioChStat(TITLE_SAVE_FOLDER_FULL"/icon.sys", &stat, FIO_CST_ATTR);
			}
		} else
			result = fd;
	}
	else
	{	//Already exists.
		fileXioClose(fd);
		result = 0;
	}

	return result;
}

static const char *WriteBoolean(int value)
{
	return(value ? "true" : "false");
}

static int WriteSettings(void)
{
	iox_stat_t stat;
	int fd, result, len;
	char *buffer;

	if((buffer = memalign(64, 512)) != NULL)
	{
		len = sprintf(buffer,	"SORT = %s\n"
					"DHCP = %s\n"
					"IP = %u.%u.%u.%u\n"
					"NM = %u.%u.%u.%u\n"
					"GW = %u.%u.%u.%u\n"
					"ADVNETWORK = %s\n"
					"LINKMODE = %d\n"
					"FLOWCTRL = %s\n",
					WriteBoolean(RuntimeData.SortTitles),
					WriteBoolean(RuntimeData.UseDHCP),
					RuntimeData.ip_address[0], RuntimeData.ip_address[1], RuntimeData.ip_address[2], RuntimeData.ip_address[3],
					RuntimeData.subnet_mask[0], RuntimeData.subnet_mask[1], RuntimeData.subnet_mask[2], RuntimeData.subnet_mask[3],
					RuntimeData.gateway[0], RuntimeData.gateway[1], RuntimeData.gateway[2], RuntimeData.gateway[3],
					WriteBoolean(RuntimeData.AdvancedNetworkSettings),
					RuntimeData.EthernetLinkMode,
					WriteBoolean(RuntimeData.EthernetFlowControl)) + 1;

		if((fd = fileXioOpen(TITLE_SAVE_FOLDER_FULL"/settings.ini", O_WRONLY | O_CREAT | O_TRUNC, TITLE_SAVE_FILE_MODE)) >= 0)
		{
			result = fileXioWrite(fd, buffer, len) == len ? 0 : -EIO;
			fileXioClose(fd);

			if(result == 0)
			{
				stat.attr = TITLE_SAVE_FILE_ATTR;
				fileXioChStat(TITLE_SAVE_FOLDER_FULL"/icon.sys", &stat, FIO_CST_ATTR);
			}
		} else
			result = fd;

		free(buffer);
	} else
		result = -ENOMEM;

	return result;
}

int SaveSettings(void)
{
	iox_stat_t stat;
	int result, fd;

	if((result=fileXioMount("pfs1:", "hdd0:__common", FIO_MT_RDWR)) >= 0)
	{
		result = fileXioMkdir(COMMON_SAVE_FOLDER, COMMON_SAVE_FOLDER_MODE);

		if (result == -EEXIST)
			result = 0;

		if(result == 0)
		{
			if((fd = fileXioOpen(TITLE_SAVE_FOLDER_FULL"/icon.sys", O_RDONLY)) >= 0)
			{
				result = 0;
				fileXioClose(fd);
			}
			else
			{	//Could not open. Probably not created.
				//Verify that there is enough space.
				if(!CheckSpace(TITLE_SAVE_MIN_FREE_SPC))
					result = -ENOSPC;

				if(result == 0)
				{
					//Attempt to create the directory.
					result = fileXioMkdir(TITLE_SAVE_FOLDER_FULL, TITLE_SAVE_FOLDER_MODE);

					if(result == 0)
					{
						stat.attr = TITLE_SAVE_FOLDER_ATTR;
						fileXioChStat(TITLE_SAVE_FOLDER_FULL, &stat, FIO_CST_ATTR);
					}
					else if (result == -EEXIST)
					{
						result = 0;
					}
				}
			}
		}

		if(result == 0)
		{	//Write the icon.
			if((result = WriteSaveIcon()) == 0)
			{	//Write the settings file.
				if((result = WriteSettings()) == 0)
				{	//Write the icon.sys file last. User can identify the corrupt data and delete it, if writing failed along the way.
					result = WriteSaveIconSys();
				}
			}
		}

		fileXioUmount("pfs1:");
	}

	return result;
}
