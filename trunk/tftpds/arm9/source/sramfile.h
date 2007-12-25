#pragma once

#include "file.h"
void BackupSRAM();

class SramFile : public File
{
public:
	SramFile(const char* filename, bool write);
	virtual ~SramFile();

	virtual int Read(void* dest, int length);
	virtual void Write(void* source, int length);
	virtual void Close();

private:
	u8* filePtr;
	FileState state;
};
