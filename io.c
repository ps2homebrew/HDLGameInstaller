#include <errno.h>
#include <kernel.h>
#include <tamtypes.h>
#include <string.h>
#include <malloc.h>
#include <fileXio_rpc.h>

#include <ps2lib_err.h>

#include "io.h"

enum IO_THREAD_CMD {
	IO_THREAD_CMD_NONE = 0,
	IO_THREAD_CMD_WRITE,
	IO_THREAD_CMD_WRITE_END,		//Finish writing outstanding data and terminate.
	IO_THREAD_CMD_READ,
	IO_THREAD_CMD_READ_END
};

struct IOState {
	void *buffer;
	struct BuffDesc *bd;
	unsigned short int WritePtr, ReadPtr;
	unsigned short int bufmax, nbufs;
	unsigned char state, command;
	unsigned int remaining;
	int id, CmdAckSema, inBufSema, outBufSema, ioFD;
};

static struct IOState IOState;

static u8 IOThreadStack[0x600] __attribute__((aligned(16)));

static int IOExecWriteNext(void)
{
	void *buffer;
	const struct BuffDesc *bd, *bdNext;
	unsigned short int nextReadPtr;
	int result, toWrite, partitionsCleared, i;

	result = 0;
	if(PollSema(IOState.outBufSema) == IOState.outBufSema)
	{
		buffer = (u8*)IOState.buffer + IOState.ReadPtr * (unsigned int)IOState.bufmax * 2048;
		bd = &IOState.bd[IOState.ReadPtr];
		toWrite = bd->length;

		//Determine how much can be written at once, without wrapping around the ring buffer.
		for(partitionsCleared = 1, nextReadPtr = IOState.ReadPtr + 1;
			nextReadPtr < IOState.nbufs;
			partitionsCleared++, nextReadPtr++) {

			if(PollSema(IOState.outBufSema) != IOState.outBufSema)
				break;

			bdNext = &IOState.bd[nextReadPtr];
			toWrite += bdNext->length;
		}

		do{
			result = fileXioWrite(IOState.ioFD, buffer, toWrite);
		}while(result < 0 && result != -EIO);

		if(result >= 0)
		{	//Update state
			IOState.ReadPtr = (IOState.ReadPtr + partitionsCleared) % IOState.nbufs;
			for (i = 0; i < partitionsCleared; i++)
				SignalSema(IOState.inBufSema);
		}
	}

	return result;
}

static int IOExecReadNext(void)
{
	void *buffer;
	struct BuffDesc *bd, *bdNext;
	unsigned short int sectors, nextWritePtr;
	unsigned int sectorsTotal;
	int result, partitionsCleared, i;

	result = 0;
	while(IOState.remaining > 0)
	{
		if (PollSema(IOState.inBufSema) == IOState.inBufSema) {
			sectors = IOState.remaining < IOState.bufmax ? IOState.remaining : IOState.bufmax;
			sectorsTotal = sectors;

			buffer = (u8*)IOState.buffer + IOState.WritePtr * (unsigned int)IOState.bufmax * 2048;
			bd = &IOState.bd[IOState.WritePtr];
			bd->length = sectors * 2048; //Indicate how many bytes will be in this partition.
			partitionsCleared = 1;

			//Determine how much can be written at once, without wrapping around the ring buffer.
			for(partitionsCleared = 1, nextWritePtr = IOState.WritePtr + 1;
				(nextWritePtr < IOState.nbufs) && (IOState.remaining - sectorsTotal > 0);
				partitionsCleared++, nextWritePtr++) {

				if(PollSema(IOState.inBufSema) != IOState.inBufSema)
					break;

				bd = &IOState.bd[nextWritePtr];
				sectors = IOState.remaining - sectorsTotal < IOState.bufmax ? IOState.remaining - sectorsTotal : IOState.bufmax;
				bd->length = sectors * 2048; //Indicate how many bytes will be in this partition.
				sectorsTotal += sectors;
			}

			//Read raw data
			do
			{
				result = fileXioRead(IOState.ioFD, buffer, sectorsTotal * 2048);
			}while(result < 0 && result != -EIO);

			if(result >= 0)
			{
				//Update state
				IOState.WritePtr = (IOState.WritePtr + partitionsCleared) % IOState.nbufs;
				for (i = 0; i < partitionsCleared; i++)
					SignalSema(IOState.outBufSema);
				IOState.remaining -= sectorsTotal;
			}
		} else
			break;
	}

	return result;
}

static void IOThread(void *arg)
{
	int result;

	while (1)
	{
		SleepThread();

		switch (IOState.command)
		{
			case IO_THREAD_CMD_WRITE:
				if (IOExecWriteNext() < 0)
				{
					IOState.state = IO_THREAD_STATE_ERROR;
					ExitDeleteThread();
				}
				break;
			case IO_THREAD_CMD_WRITE_END:
				//Finish writing all data
				while ((result = IOExecWriteNext()) > 0){};
				if(result < 0)
					IOState.state = IO_THREAD_STATE_ERROR;

				SignalSema(IOState.CmdAckSema);
				ExitDeleteThread();
				break;
			case IO_THREAD_CMD_READ:
				if (IOExecReadNext() < 0)
				{
					IOState.state = IO_THREAD_STATE_ERROR;
					ExitDeleteThread();
				}
				break;
			case IO_THREAD_CMD_READ_END:
				SignalSema(IOState.CmdAckSema);
				ExitDeleteThread();
				break;
		}
	}
}

static void IOExec(unsigned char cmd)
{
	IOState.command = cmd;
	WakeupThread(IOState.id);
}

static void IOExecWait(unsigned char cmd)
{
	IOState.command = cmd;
	WakeupThread(IOState.id);
	WaitSema(IOState.CmdAckSema);
}

static int IOInitCommon(int fd, struct BuffDesc *bd, void *buffers, unsigned short int bufmax, unsigned short int nbufs, int prio)
{
	ee_sema_t sema;
	int result;

	sema.init_count = 0;
	sema.max_count = 1;
	sema.attr = 0;
	sema.option = (u32)"io-CmdAck";
	if ((IOState.CmdAckSema = CreateSema(&sema)) >= 0)
	{
		sema.init_count = nbufs;
		sema.max_count = nbufs;
		sema.attr = 0;
		sema.option = (u32)"io-inBufSema";
		if ((IOState.inBufSema = CreateSema(&sema)) >= 0)
		{
			sema.init_count = 0;
			sema.max_count = nbufs;
			sema.attr = 0;
			sema.option = (u32)"io-outBufSema";
			if ((IOState.outBufSema = CreateSema(&sema)) >= 0)
			{
				IOState.command = IO_THREAD_CMD_NONE;
				IOState.state = IO_THREAD_STATE_OK;
				IOState.bd = bd;
				IOState.buffer = buffers;
				IOState.bufmax = bufmax;
				IOState.nbufs = nbufs;
				IOState.WritePtr = 0;
				IOState.ReadPtr = 0;
				IOState.ioFD = fd;
				IOState.id = SysCreateThread(&IOThread, IOThreadStack, sizeof(IOThreadStack), &IOState, prio);

				result = IOState.id;
				if (IOState.id < 0)
				{
					DeleteSema(IOState.CmdAckSema);
					DeleteSema(IOState.inBufSema);
				}
			}
			else
			{
				DeleteSema(IOState.inBufSema);
				DeleteSema(IOState.CmdAckSema);
				result = IOState.outBufSema;
			}
		}
		else
		{
			DeleteSema(IOState.CmdAckSema);
			result = IOState.inBufSema;
		}
	}
	else
		result = IOState.CmdAckSema;

	return result;
}

int IOWriteInit(int fd, struct BuffDesc *bd, void *buffers, unsigned short int bufmax, unsigned short int nbufs, int prio)
{
	return IOInitCommon(fd, bd, buffers, bufmax, nbufs, prio);
}

int IOReadInit(int fd, struct BuffDesc *bd, void *buffers, unsigned short int bufmax, unsigned short int nbufs, unsigned int remaining, int prio)
{
	int result;
	IOState.remaining = remaining;
	if((result = IOInitCommon(fd, bd, buffers, bufmax, nbufs, prio)) >= 0)
		IOExec(IO_THREAD_CMD_READ);

	return result;
}

//Note: a call to IOSignalWriteDone() must follow a call to IOGetNextWrBuffer()!
void *IOGetNextWrBuffer(void)
{
	WaitSema(IOState.inBufSema);
	return((u8*)IOState.buffer + IOState.bufmax * (unsigned int)IOState.WritePtr * 2048);
}

void IOSignalWriteDone(int length)
{
	struct BuffDesc *bd;

	bd = &IOState.bd[IOState.WritePtr];
	bd->length = length;
	IOState.WritePtr = (IOState.WritePtr + 1) % IOState.nbufs;
	SignalSema(IOState.outBufSema);
	IOExec(IO_THREAD_CMD_WRITE);
}

int IORead(void *buffer)
{
	struct BuffDesc *bd;
	const void *pReadData;
	int length;

	WaitSema(IOState.outBufSema);
	bd = &IOState.bd[IOState.ReadPtr];
	pReadData = (const void*)((u8*)IOState.buffer + IOState.ReadPtr * (unsigned int)IOState.bufmax * 2048);
	IOState.ReadPtr = (IOState.ReadPtr + 1) % IOState.nbufs;

	//Copy out the data read
	memcpy(buffer, pReadData, bd->length);
	length = (int)bd->length;

	SignalSema(IOState.inBufSema);

	//Continue reading
	IOExec(IO_THREAD_CMD_READ);

	return length;
}

int IOReadNext(const void **buffer)
{
	struct BuffDesc *bd;
	int length;

	WaitSema(IOState.outBufSema);
	bd = &IOState.bd[IOState.ReadPtr];
	*buffer = (const void*)((u8*)IOState.buffer + IOState.ReadPtr * (unsigned int)IOState.bufmax * 2048);
	IOState.ReadPtr = (IOState.ReadPtr + 1) % IOState.nbufs;
	length = (int)bd->length;

	return length;
}

void IOReadAdvance(void)
{
	SignalSema(IOState.inBufSema);

	//Continue reading
	IOExec(IO_THREAD_CMD_READ);
}

static void IOEnd(void)
{
	DeleteSema(IOState.CmdAckSema);
	DeleteSema(IOState.inBufSema);
	DeleteSema(IOState.outBufSema);
}

int IOEndWrite(void)
{
	IOExecWait(IO_THREAD_CMD_WRITE_END);
	IOEnd();

	return IOState.state;
}

int IOEndRead(void)
{
	IOExecWait(IO_THREAD_CMD_READ_END);
	IOEnd();

	return IOState.state;
}

int IOGetStatus(void)
{
	return IOState.state;
}

int IOAlloc(struct BuffDesc **bdOut, void **ioBufferOut)
{
	struct BuffDesc *bd;
	void *ioBuffer;
	int result;

	if((bd = malloc(sizeof(struct BuffDesc) * IO_BANKMAX)) != NULL)
	{
		if((ioBuffer = memalign(64, IO_BANKMAX * IO_BANKSIZE * 2048)) != NULL)
		{
			*bdOut = bd;
			*ioBufferOut = ioBuffer;
			result = 0;
		}
		else
		{
			free(bd);
			result = -ENOMEM;
		}
	}
	else
		result = -ENOMEM;

	return result;
}

void IOFree(struct BuffDesc **bdOut, void **ioBufferOut)
{
	if(*bdOut != NULL)
	{
		free(*bdOut);
		*bdOut = NULL;
	}
	if(*ioBufferOut != NULL)
	{
		free(*ioBufferOut);
		*ioBufferOut = NULL;
	}
}
