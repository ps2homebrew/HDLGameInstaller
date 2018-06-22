#include <libpad.h>
#include <stdio.h>
#include <tamtypes.h>

#include "pad.h"

static unsigned int old_pad[2]={0, 0};

int ReadPadStatus_raw(int port, int slot){
	struct padButtonStatus buttons;
	unsigned int paddata;

	paddata=0;
	if(padRead(port, slot, &buttons) != 0){
		paddata = 0xffff ^ buttons.btns;
	}

	return paddata;
}

int ReadCombinedPadStatus_raw(void){
	return(ReadPadStatus_raw(0, 0)|ReadPadStatus_raw(1, 0));
}

void WaitPadClear(int port, int slot){
	while(ReadPadStatus_raw(port, slot)!=0){};
}

int ReadPadStatus(int port, int slot){
	struct padButtonStatus buttons;
	unsigned int paddata, new_pad;

	new_pad=0;
	if(padRead(port, slot, &buttons) != 0){
		paddata = 0xffff ^ buttons.btns;
		new_pad = paddata & ~old_pad[port];
		old_pad[port] = paddata;
	}

	return new_pad;
}

int ReadCombinedPadStatus(void){
	return(ReadPadStatus(0, 0)|ReadPadStatus(1, 0));
}
