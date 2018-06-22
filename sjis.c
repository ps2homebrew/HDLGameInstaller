#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "sjis.h"

static unsigned int *LookupTable;
static unsigned int NumLookupTableEntries;

int SetSJISToUnicodeLookupTable(void *table, unsigned int TableLength){
	LookupTable=table;
	NumLookupTableEntries=TableLength/sizeof(unsigned int)/2;
	return 0;
}

wchar_t ConvertSJISToUnicodeChar(unsigned short int SourceChar){
	unsigned int index;
	wchar_t UnicodeChar;

	if(SourceChar<0x80){
		UnicodeChar=SourceChar;
	}
	else{
		UnicodeChar=UNICODE_REPLACEMENT_CHAR;
		for(index=0; index<NumLookupTableEntries; index++){
			if(LookupTable[index*2+0]==SourceChar){
				UnicodeChar=LookupTable[index*2+1];
				break;
			}
		}
	}

	return UnicodeChar;
}

int SJISToUnicode(const unsigned char *SJISStringIn, int length, wchar_t *UnicodeStringOut, unsigned int NumUChars){
	unsigned int i, NumChars;
	const char *SourceString;
	unsigned short int SJISCharacter;
	int result;

	for(result=0,i=0,SourceString=SJISStringIn,NumChars=0; NumChars<NumUChars; i+=GetSJISCharLength(SJISCharacter),NumChars++){
		if(length>0 && i>=length) break;

		if((SJISCharacter=GetNextSJISChar(&SourceString))=='\0'){
			UnicodeStringOut[NumChars++]='\0';
			break;
		}

		UnicodeStringOut[NumChars]=ConvertSJISToUnicodeChar(SJISCharacter);
	}

	return(result==0?NumChars:result);
}

int GetSJISCharLengthFromString(const char *SJISCharacter){
	return((SJISCharacter[0]&0x80)?2:1);
}

int GetSJISCharLength(unsigned short int SJISCharacter){
	return((SJISCharacter&0x8000)?2:1);
}

unsigned short int GetNextSJISChar(const char **string){
	int CharLength;
	unsigned short int NextSJISChar;

	CharLength=GetSJISCharLengthFromString(*string);
	if(CharLength==1){
		NextSJISChar=(*string)[0];
		(*string)++;
	}
	else{
		((unsigned char*)&NextSJISChar)[1]=(*string)[0];
		((unsigned char*)&NextSJISChar)[0]=(*string)[1];
		(*string)+=2;
	}

	return NextSJISChar; 
}
