/* $Id$ */

/*
 * This file is part of FreeRCT.
 * FreeRCT is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * FreeRCT is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with FreeRCT. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file fileio.cpp File IO functions. */

/**
 * @defgroup fileio_group File IO
 */

#include "stdafx.h"
#include "fileio.h"
#include "string_func.h"
#ifdef LINUX
	#include "unix/fileio_unix.h"
	#include <dirent.h>
	#include <unistd.h>
#elif WINDOWS
	#include "windows/fileio_windows.h"
	#include <direct.h> // contains chdir in windows
#endif
#include <sys/types.h>
#include <sys/stat.h>

/**
 * Base class implementation of a directory reader, never returning any content.
 * Derive a new class for your operating system with more functionality.
 * @param dir_sep Directory separator string of the operating system.
 */
DirectoryReader::DirectoryReader(const char dir_sep) : dir_sep(dir_sep)
{
}

/** Destructor. */
DirectoryReader::~DirectoryReader()
{
}

/**
 * @fn void DirectoryReader::OpenPath(const char *path)
 * Set up the directory reader object for reading a directory.
 * @param path Path to the directory.
 * @note Failure is not reported here, but #NextEntry will not return anything useful.
 */

/**
 * @fn const char *DirectoryReader::NextEntry()
 * Get next entry of the directory contents.
 * @return Pointer to name of next entry (as a file path suitable for opening a file). Returns \c nullptr if not next entry exists.
 * @note The memory returned is owned by the #DirectoryReader object, and should not be released.
 */

/**
 * @fn void DirectoryReader::ClosePath()
 * Denote no further interest in reading the contents of the current directory.
 */

/**
 * @fn const char *DirectoryReader::MakePath(const char *directory, const char *fname)
 * Construct a path from a directory and a file name.
 * @param directory Directory part of the path.
 * @param fname File name part of the path.
 * @return The combined path to the file.
 */

/**
 * @fn bool DirectoryReader::EntryIsFile()
 * Test whether the last returned entry from #NextEntry is a file.
 * @return Whether the entry is a file.
 */

/**
 * @fn bool DirectoryReader::EntryIsDirectory()
 * Test whether the last returned entry from #NextEntry is a directory.
 * @return Whether the entry is a directory.
 */

/**
 * Get the next file entry.
 * Pulls entries from #NextEntry, until the end is reached or until a file is returned.
 * @return Next file entry, if available.
 */
const char *DirectoryReader::NextFile()
{
	const char *entry;

	do {
		entry = this->NextEntry();
	} while (entry != nullptr && !this->EntryIsFile());
	return entry;
}


/**
 * Construct a directory reader object (specific for the operating system).
 * @return A directory reader.
 * @ingroup fileio_group
 */
DirectoryReader *MakeDirectoryReader()
{
#ifdef LINUX
	return new UnixDirectoryReader();
#elif WINDOWS
	return new WindowsDirectoryReader();
#else
	assert_compile(false);
#endif
}

/**
 * RCD file reader constructor, loading data from a file.
 * @param fname Name of the file to load.
 */
RcdFileReader::RcdFileReader(const char *fname)
{
	this->file_pos = 0;
	this->file_size = 0;
	this->name[4] = '\0';

	this->fp = fopen(fname, "rb");
	if (this->fp == nullptr) return;

	fseek(this->fp, 0L, SEEK_END);
	this->file_size = ftell(this->fp);
	fseek(this->fp, 0L, SEEK_SET);
}

/** Destructor. */
RcdFileReader::~RcdFileReader()
{
	if (this->fp != nullptr) fclose(fp);
}

/**
 * Get length of data not yet read.
 * @return Count of remaining data.
 */
size_t RcdFileReader::GetRemaining()
{
	return (this->file_size >= this->file_pos) ? this->file_size - this->file_pos : 0;
}

/**
 * Read an 8 bits unsigned number.
 * @return Loaded number.
 * @pre File must be open, data must be available.
 */
uint8 RcdFileReader::GetUInt8()
{
	this->file_pos++;
	return fgetc(this->fp);
}

/**
 * Read an 8 bits signed number.
 * @return Loaded number.
 * @pre File must be open, data must be available.
 */
int8 RcdFileReader::GetInt8()
{
	return this->GetUInt8();
}

/**
 * Read an 16 bits unsigned number.
 * @return Loaded number.
 * @pre File must be open, data must be available.
 */
uint16 RcdFileReader::GetUInt16()
{
	uint16 val = this->GetUInt8();
	return val | (this->GetUInt8() << 8);
}

/**
 * Read an 16 bits signed number.
 * @return Loaded number.
 * @pre File must be open, data must be available.
 */
int16 RcdFileReader::GetInt16()
{
	uint16 val = this->GetUInt8();
	return val | (this->GetUInt8() << 8);
}

/**
 * Read an 32 bits unsigned number.
 * @return Loaded number.
 * @pre File must be open, data must be available.
 */
uint32 RcdFileReader::GetUInt32()
{
	uint32 val = this->GetUInt16();
	return val | (this->GetUInt16() << 16);
}

/**
 * Read an 32 bits signed number.
 * @return Loaded number.
 * @pre File must be open, data must be available.
 */
int32 RcdFileReader::GetInt32()
{
	uint32 val = this->GetUInt16();
	return val | (this->GetUInt16() << 16);
}

/**
 * Check whether the file header makes sense, and has the right version.
 * @param hdr_name Header name (should be 4 chars long).
 * @param version Header version.
 * @return The header seems correct.
 */
bool RcdFileReader::CheckFileHeader(const char *hdr_name, uint32 version)
{
	if (this->fp == nullptr) return false;
	if (this->GetRemaining() < 8) return false;

	char name[5];
	this->GetBlob(name, 4);
	name[4] = '\0';
	if (strcmp(name, hdr_name) != 0) return false;
	uint32 val = this->GetUInt32();
	return val == version;
}

/**
 * Starting at the first byte of a block, read the block information, and put it in #name, #version, and #size.
 * @return Whether a block was found. If so, data is in #name, #version, and #size.
 */
bool RcdFileReader::ReadBlockHeader()
{
	if (this->GetRemaining() < 12) return false;
	this->GetBlob(this->name, 4);
	this->version = this->GetUInt32();
	this->size = this->GetUInt32();
	return this->file_pos + (size_t)this->size <= this->file_size;
}

/**
 * Skip a number of bytes in the file.
 * @param count Number of bytes to move forward.
 * @return Skipping was successful.
 */
bool RcdFileReader::SkipBytes(uint32 count)
{
	this->file_pos += count;
	if (this->file_pos > this->file_size) this->file_pos = this->file_size;
	return fseek(this->fp, this->file_pos, SEEK_SET) == 0;
}

/**
 * Get a blob of data from the file.
 * @param address Address to load into.
 * @param length Length of the data.
 * @return Loading was successful.
 */
bool RcdFileReader::GetBlob(void *address, size_t length)
{
	this->file_pos += length;
	return fread(address, length, 1, this->fp) == 1;
}

/**
 * Attempts to change the working directory to one in which the executable resides in.
 * @param exe "Path" to the executable (from argv[0]).
 * @return If the working directory was changed successfully.
 */
bool ChangeWorkingDirectoryToExecutable(const char *exe)
{
	DirectoryReader *dirread = MakeDirectoryReader();
	const char dir_sep = dirread->dir_sep;
	delete dirread;

	bool success = false;
	char *s = const_cast<char *>(strrchr(exe, dir_sep));
	if (s != NULL) {
		*s = '\0';
		if (chdir(exe) != 0) {
			fprintf(stderr, "Directory with the binary does not exist?");
		} else {
			success = true;
		}
		*s = dir_sep;
	}
	return success;
}
