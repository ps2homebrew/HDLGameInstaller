#include <errno.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>

#include <fileXio_rpc.h>

#include "ipconfig.h"

static char *GetNextToken(char *line, char delimiter){
	char *field_end, *result;
	static char *current_line=NULL;

	result=NULL;

	if(line!=NULL){
		current_line=line;
	}

	while(*current_line==delimiter) current_line++;
	if(current_line[0]!='\0'){
		if((field_end=strchr(current_line, delimiter))==NULL){
			field_end=&current_line[strlen(current_line)];
		}

		*field_end='\0';

		if(current_line[1]!='\0'){	//Test to see if there is another token after this one.
			result=current_line;
			current_line=field_end+1;
		}
		else current_line=NULL;
	}
	else current_line=NULL;

	return result;
}

int ParseConfig(const char *path, char *ip_address, char *subnet_mask, char *gateway){
	int fd, result, size;
	char *FileBuffer, *line, *field;
	unsigned int i, DataLineNum;

	if((fd=fileXioOpen(path, O_RDONLY))>=0){
		size=fileXioLseek(fd, 0, SEEK_END);
		fileXioLseek(fd, 0, SEEK_SET);
		if((FileBuffer=malloc(size))!=NULL){
			if(fileXioRead(fd, FileBuffer, size)==size){
				if((line=strtok(FileBuffer, "\r\n"))!=NULL){
					result=EINVAL;
					DataLineNum=0;
					do{
						i=0;
						while(line[i]==' ') i++;
						if(line[i]!='#' && line[i]!='\0'){
							if(DataLineNum==0){
								if((field=GetNextToken(line, ' '))!=NULL){
									strncpy(ip_address, field, 15);
									ip_address[15]='\0';
									if((field=GetNextToken(NULL, ' '))!=NULL){
										strncpy(subnet_mask, field, 15);
										subnet_mask[15]='\0';
										if((field=GetNextToken(NULL, ' '))!=NULL){
											strncpy(gateway, field, 15);
											gateway[15]='\0';
											result=0;
											DataLineNum++;
										}
									}
								}
							}
						}
					}while((line=strtok(NULL, "\r\n"))!=NULL);
				}
				else result=EINVAL;
			}
			else result=EIO;

			free(FileBuffer);
		}
		else result=ENOMEM;

		fileXioClose(fd);
	}
	else result=fd;

	return result;
}
