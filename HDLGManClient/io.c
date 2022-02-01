#include <Windows.h>
#include <errno.h>
#include <string.h>
#include <malloc.h>

#include "system.h"
#include "iso9660.h"
#include "io.h"

enum IO_THREAD_CMD {
    IO_THREAD_CMD_NONE = 0,
    IO_THREAD_CMD_WRITE,
    IO_THREAD_CMD_WRITE_END, // Finish writing outstanding data and terminate.
    IO_THREAD_CMD_READ,
    IO_THREAD_CMD_READ_END
};

struct IOState
{
    void *buffer;
    struct BuffDesc *bd;
    unsigned short int WritePtr, ReadPtr;
    unsigned short int bufmax, nbufs;
    unsigned char state, command, sectorType;
    unsigned int remaining, lsn;
    unsigned int opt;
    HANDLE id, CmdAckEvent, InBufSema, OutBufSema, ioFD, IoThreadEvent;
};

static struct IOState IOState;

static int IOExecWriteNext(void)
{
    void *buffer;
    const struct BuffDesc *bd;
    int result;

    result = 0;
    while (WaitForSingleObject(IOState.OutBufSema, 0) == WAIT_OBJECT_0) {
        buffer = (unsigned char *)IOState.buffer + IOState.ReadPtr * (unsigned int)IOState.bufmax * 2048;
        bd     = &IOState.bd[IOState.ReadPtr];

        do {
            result = writeFile(IOState.ioFD, buffer, bd->length);
        } while (result < 0 && result != -EIO);

        if (result >= 0) { // Update state
            IOState.ReadPtr = (IOState.ReadPtr + 1) % IOState.nbufs;
            ReleaseSemaphore(IOState.InBufSema, 1, NULL);
        }
    }

    return result;
}

static int IOExecReadNext(void)
{
    void *buffer;
    struct BuffDesc *bd;
    unsigned short int sectors;
    unsigned int bytes;
    int result;

    result = 0;
    while ((IOState.remaining > 0) && (WaitForSingleObject(IOState.InBufSema, 0) == WAIT_OBJECT_0)) {
        sectors = IOState.remaining < IOState.bufmax ? IOState.remaining : IOState.bufmax;
        bytes   = (unsigned int)sectors * 2048;

        buffer = (unsigned char *)IOState.buffer + IOState.WritePtr * (unsigned int)IOState.bufmax * 2048;
        bd     = &IOState.bd[IOState.WritePtr];

        // Read raw data
        do {
            result = ReadSectors(IOState.ioFD, IOState.sectorType, IOState.lsn, sectors, buffer);
        } while (result < 0 && result != -EIO);

        if (result >= 0) {
            bd->length = bytes;

            // Update state
            IOState.WritePtr = (IOState.WritePtr + 1) % IOState.nbufs;
            ReleaseSemaphore(IOState.OutBufSema, 1, NULL);
            IOState.remaining -= sectors;
            IOState.lsn += sectors;
        }
    }

    return result;
}

static DWORD WINAPI IOThread(void *arg)
{
    int result;

    while (1) {
        WaitForSingleObject(IOState.IoThreadEvent, INFINITE);

        switch (IOState.command) {
            case IO_THREAD_CMD_WRITE:
                if (IOExecWriteNext() < 0) {
                    IOState.state = IO_THREAD_STATE_ERROR;
                    return IO_THREAD_STATE_ERROR;
                }
                break;
            case IO_THREAD_CMD_WRITE_END:
                // Finish writing all data
                while ((result = IOExecWriteNext()) > 0) {};
                if (result < 0)
                    IOState.state = IO_THREAD_STATE_ERROR;

                SetEvent(IOState.CmdAckEvent);
                return 0;
            case IO_THREAD_CMD_READ:
                if (IOExecReadNext() < 0) {
                    IOState.state = IO_THREAD_STATE_ERROR;
                    return IO_THREAD_STATE_ERROR;
                }
                break;
            case IO_THREAD_CMD_READ_END:
                SetEvent(IOState.CmdAckEvent);
                return 0;
        }
    }
}

static void IOExec(unsigned char cmd)
{
    IOState.command = cmd;
    SetEvent(IOState.IoThreadEvent);
}

static void IOExecWait(unsigned char cmd)
{
    IOState.command = cmd;
    SetEvent(IOState.IoThreadEvent);
    WaitForSingleObject(IOState.CmdAckEvent, INFINITE);
}

static int IOInitCommon(HANDLE fd, struct BuffDesc *bd, void *buffers, unsigned short int bufmax, unsigned short int nbufs)
{
    int result;

    if ((IOState.IoThreadEvent = CreateEvent(NULL, FALSE, FALSE, L"io-CmdThread")) != NULL) {
        if ((IOState.CmdAckEvent = CreateEvent(NULL, FALSE, FALSE, L"io-CmdAck")) != NULL) {
            if ((IOState.InBufSema = CreateSemaphore(NULL, nbufs, nbufs, L"io-InBufSema")) != NULL) {
                if ((IOState.OutBufSema = CreateSemaphore(NULL, 0, nbufs, L"io-OutBufSema")) != NULL) {
                    IOState.command  = IO_THREAD_CMD_NONE;
                    IOState.state    = IO_THREAD_STATE_OK;
                    IOState.bd       = bd;
                    IOState.buffer   = buffers;
                    IOState.bufmax   = bufmax;
                    IOState.nbufs    = nbufs;
                    IOState.WritePtr = 0;
                    IOState.ReadPtr  = 0;
                    IOState.ioFD     = fd;
                    IOState.id       = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&IOThread, NULL, 0, NULL);

                    if (IOState.id == NULL) {
                        result = GetLastError();
                        CloseHandle(IOState.IoThreadEvent);
                        CloseHandle(IOState.CmdAckEvent);
                        CloseHandle(IOState.InBufSema);
                    } else
                        result = 0;
                } else {
                    CloseHandle(IOState.IoThreadEvent);
                    CloseHandle(IOState.InBufSema);
                    CloseHandle(IOState.CmdAckEvent);
                    result = GetLastError();
                }
            } else {
                CloseHandle(IOState.IoThreadEvent);
                CloseHandle(IOState.CmdAckEvent);
                result = GetLastError();
            }
        } else {
            CloseHandle(IOState.IoThreadEvent);
            result = GetLastError();
        }
    } else
        result = GetLastError();

    return result;
}

int IOWriteInit(HANDLE fd, struct BuffDesc *bd, void *buffers, unsigned short int bufmax, unsigned short int nbufs)
{
    return IOInitCommon(fd, bd, buffers, bufmax, nbufs);
}

int IOReadInit(HANDLE fd, struct BuffDesc *bd, void *buffers, unsigned short int bufmax, unsigned short int nbufs, unsigned int remaining, unsigned char sectorType)
{
    int result;
    IOState.remaining  = remaining;
    IOState.lsn        = 0;
    IOState.sectorType = sectorType;
    if ((result = IOInitCommon(fd, bd, buffers, bufmax, nbufs)) >= 0)
        IOExec(IO_THREAD_CMD_READ);

    return result;
}

// Note: a call to IOSignalWriteDone() must follow a call to IOGetNextWrBuffer()!
void *IOGetNextWrBuffer(void)
{
    WaitForSingleObject(IOState.InBufSema, INFINITE);
    return ((unsigned char *)IOState.buffer + IOState.bufmax * (unsigned int)IOState.WritePtr * 2048);
}

void IOSignalWriteDone(int length)
{
    struct BuffDesc *bd;

    bd               = &IOState.bd[IOState.WritePtr];
    bd->length       = length;
    IOState.WritePtr = (IOState.WritePtr + 1) % IOState.nbufs;
    ReleaseSemaphore(IOState.OutBufSema, 1, NULL);
    IOExec(IO_THREAD_CMD_WRITE);
}

int IORead(void *buffer)
{
    struct BuffDesc *bd;
    const void *pReadData;
    int length;

    WaitForSingleObject(IOState.OutBufSema, INFINITE);
    bd              = &IOState.bd[IOState.ReadPtr];
    pReadData       = (const void *)((unsigned char *)IOState.buffer + IOState.ReadPtr * (unsigned int)IOState.bufmax * 2048);
    IOState.ReadPtr = (IOState.ReadPtr + 1) % IOState.nbufs;

    // Copy out the data read
    memcpy(buffer, pReadData, bd->length);
    length = (int)bd->length;

    ReleaseSemaphore(IOState.InBufSema, 1, NULL);

    // Continue reading
    IOExec(IO_THREAD_CMD_READ);

    return length;
}

int IOReadNext(const void **buffer)
{
    struct BuffDesc *bd;
    int length;

    WaitForSingleObject(IOState.OutBufSema, INFINITE);
    bd              = &IOState.bd[IOState.ReadPtr];
    *buffer         = (const void *)((unsigned char *)IOState.buffer + IOState.ReadPtr * (unsigned int)IOState.bufmax * 2048);
    IOState.ReadPtr = (IOState.ReadPtr + 1) % IOState.nbufs;
    length          = (int)bd->length;

    return length;
}

void IOReadAdvance(void)
{
    ReleaseSemaphore(IOState.InBufSema, 1, NULL);

    // Continue reading
    IOExec(IO_THREAD_CMD_READ);
}

static void IOEnd(void)
{
    CloseHandle(IOState.IoThreadEvent);
    CloseHandle(IOState.CmdAckEvent);
    CloseHandle(IOState.InBufSema);
    CloseHandle(IOState.OutBufSema);
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

    if ((bd = malloc(sizeof(struct BuffDesc) * IO_BANKMAX)) != NULL) {
        if ((ioBuffer = malloc(IO_BANKMAX * IO_BANKSIZE * 2048)) != NULL) {
            *bdOut       = bd;
            *ioBufferOut = ioBuffer;
            result       = 0;
        } else {
            free(bd);
            result = -ENOMEM;
        }
    } else
        result = -ENOMEM;

    return result;
}

void IOFree(struct BuffDesc **bdOut, void **ioBufferOut)
{
    if (*bdOut != NULL) {
        free(*bdOut);
        *bdOut = NULL;
    }
    if (*ioBufferOut != NULL) {
        free(*ioBufferOut);
        *ioBufferOut = NULL;
    }
}
