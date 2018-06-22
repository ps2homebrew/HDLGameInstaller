#define UNICODE_REPLACEMENT_CHAR	0xFFFD
#define SJIS_REPLACEMENT_CHAR		0x0080

int SetSJISToUnicodeLookupTable(void *table, unsigned int TableLength);

wchar_t ConvertSJISToUnicodeChar(unsigned short int SourceChar);
int SJISToUnicode(const unsigned char *SJISStringIn, int length, wchar_t *UnicodeStringOut, unsigned int NumUChars);

int GetSJISCharLength(unsigned short int SJISCharacter);
int GetSJISCharLengthFromString(const char *SJISCharacter);
unsigned short int GetNextSJISChar(const char **string);
