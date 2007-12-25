#include <nds.h>
#include <stdio.h>
#include "sramfile.h"

#define min(x, y) ((x)<=(y)?(x):(y))

#define SRAM_START ((u8*)0x0A000000)
#define SRAM_END ((u8*)0x0A03FFFF) //256KB
//#define SRAM_END ((u8*)0x0A010000) //64KB

SramFile::SramFile(const char* filename, bool write)
:	filePtr(SRAM_START),
	state(write ? FILESTATE_WRITE : FILESTATE_READ)
{
}

SramFile::~SramFile()
{
	if(state != FILESTATE_CLOSED)
	{
		try
		{
			Close();
		}
		catch(...)
		{
		}
	}
}

int SramFile::Read(void* dest, int length)
{
	if(state != FILESTATE_READ)
	{
		throw "Illegal state";
	}

	u8* writePtr = (u8*)dest;
	u8* endPtr = min(filePtr + length, SRAM_END+1);

	while(filePtr != endPtr)
	{
		*writePtr++ = *filePtr++;
	}

	return (int)(writePtr - (u8*)dest);
}

void SramFile::Write(void* source, int length)
{
	if(state != FILESTATE_WRITE)
	{
		throw "Illegal state";
	}

	if(filePtr + length > SRAM_END+1)
	{
		throw "Write outside sram";
	}

	u8 *readPtr = (u8*)source;
	for(int i = 0; i < length; i++) {
		*filePtr++ = *readPtr++;
	}
}

void SramFile::Close()
{
	state = FILESTATE_CLOSED;
}
