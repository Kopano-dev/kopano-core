/*
 * Copyright 2005 - 2016 Zarafa and its licensors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <kopano/platform.h>
#include <kopano/stringutil.h>
#include <kopano/charset/convert.h>
#include <memory>
#include <string>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <kopano/ECLogger.h>

#include <mapicode.h>
#include <mapidefs.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include "fileutil.h"

#define BLOCKSIZE	65536

namespace KC {

/** 
 * Reads the contents of a file, and writes it to the output file
 * while converting Unix \n enters to DOS \r\n enters.
 * 
 * @todo this function prints errors to screen using perror(), which should be removed
 * @todo this function doesn't return the filepointer in case of an error, but also doesn't unlink the tempfile either.
 *
 * @param[in] fin input file to read strings from
 * @param[out] fout new filepointer to a temporary file
 * 
 * @return MAPI error code
 */
HRESULT HrFileLFtoCRLF(FILE *fin, FILE** fout)
{
	char	bufferin[BLOCKSIZE / 2];
	char	bufferout[BLOCKSIZE+1];
	size_t sizebufferout;

	if(fin == NULL || fout == NULL)
		return MAPI_E_INVALID_PARAMETER;

	std::unique_ptr<FILE, file_deleter> fTmp(tmpfile());
	if(fTmp == NULL) {
		perror("Unable to create tmp file");
		return MAPI_E_CALL_FAILED;
	}

	while (!feof(fin)) {
		size_t readsize = fread(bufferin, 1, BLOCKSIZE / 2, fin);
		if (ferror(fin)) {
			perror("Read error");//FIXME: What an error?, what now?
			return MAPI_E_CORRUPT_DATA;
		}

		BufferLFtoCRLF(readsize, bufferin, bufferout, &sizebufferout);
		if (fwrite(bufferout, 1, sizebufferout, fTmp.get()) != sizebufferout) {
			perror("Write error");//FIXME: What an error?, what now?
			return MAPI_E_CORRUPT_DATA;
		}
	}

	*fout = fTmp.release();
	return hrSuccess;
}

/** 
 * align to page boundary (4k)
 * 
 * @param size add "padding" to size to make sure it's a multiple 4096 bytes
 * 
 * @return aligned size
 */
static inline int mmapsize(unsigned int size)
{
	return (((size + 1) >> 12) + 1) << 12;
}

/** 
 * Load a file, first by trying to use mmap. If that fails, the whole
 * file is loaded in memory.
 * 
 * @param[in] f file to read
 * @param[out] lppBuffer buffer containing the file contents
 * @param[out] lpSize length of the buffer
 * @param[out] lpImmap boolean denoting if the buffer is mapped or not (used when freeing the buffer)
 * 
 * @return MAPI error code
 */
static HRESULT HrMapFileToBuffer(FILE *f, char **lppBuffer, int *lpSize,
    bool *lpImmap)
{
	char *lpBuffer = NULL;
	int offset = 0;
	long ulBufferSize = BLOCKSIZE;
	struct stat stat;
	int fd = fileno(f);

	*lpImmap = false;

	/* Try mmap first */
	if (fstat(fd, &stat) != 0) {
		perror("Stat failed");
		return MAPI_E_CALL_FAILED;
	}

	/* auto-zero-terminate because mmap zeroes bytes after the file */
	lpBuffer = (char *)mmap(0, mmapsize(stat.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
	if (lpBuffer != MAP_FAILED) {
		*lpImmap = true;
		*lppBuffer = lpBuffer;
		*lpSize = stat.st_size;
		return hrSuccess;
	}

	/* mmap failed (probably reading from STDIN as a stream), just read the file into memory, and return that */
	lpBuffer = (char*)malloc(BLOCKSIZE); // will be deleted as soon as possible
	while (!feof(f)) {
		long ulReadsize = fread(lpBuffer+offset, 1, BLOCKSIZE, f);
		if (ferror(f)) {
			perror("Read error");
			break;
		}

		offset += ulReadsize;
		if (offset + BLOCKSIZE > ulBufferSize) {    // Next read could cross buffer boundary, realloc
			auto lpRealloc = static_cast<char *>(realloc(lpBuffer, offset + BLOCKSIZE));
			if (lpRealloc == NULL) {
				free(lpBuffer);
				return MAPI_E_NOT_ENOUGH_MEMORY;
			}
			lpBuffer = lpRealloc;
			ulBufferSize += BLOCKSIZE;
		}
	}

	/* Nothing was read */
    if (offset == 0) {
		free(lpBuffer);
		*lppBuffer = NULL;
		*lpSize = 0;
		return hrSuccess;
	}
	/* Add terminate character */
	lpBuffer[offset] = 0;
	*lppBuffer = lpBuffer;
	*lpSize = offset;
	return hrSuccess;
}

/** 
 * Free a buffer from HrMapFileToBuffer
 * 
 * @param[in] lpBuffer buffer to free
 * @param[in] ulSize size of the buffer
 * @param[in] bImmap marker if the buffer is mapped or not
 */
static HRESULT HrUnmapFileBuffer(char *lpBuffer, int ulSize, bool bImmap)
{
	if (bImmap)
		munmap(lpBuffer, mmapsize(ulSize));
	else
		free(lpBuffer);
	return hrSuccess;
}

/** 
 * Reads a file into a std::string using file mapping if possible.
 *
 * @todo doesn't the std::string undermine the whole idea of mapping?
 * 
 * @param[in] f file to read in memory
 * @param[out] lpstrBuffer string containing the file contents, optionally returned (why?)
 * 
 * @return 
 */
HRESULT HrMapFileToString(FILE *f, std::string *lpstrBuffer)
{
	HRESULT hr = hrSuccess;
	char *lpBuffer = NULL;
	int ulBufferSize = 0;
	bool immap = false;

	hr = HrMapFileToBuffer(f, &lpBuffer, &ulBufferSize, &immap); // what if message was half read?
	if (hr != hrSuccess || !lpBuffer)
		goto exit;

	if (lpstrBuffer)
		*lpstrBuffer = std::string(lpBuffer, ulBufferSize);
exit:
	if (lpBuffer)
		HrUnmapFileBuffer(lpBuffer, ulBufferSize, immap);

	return hr;
}

/**
 * Duplicate a file, to a given location
 *
 * @param[in]	lpFile Pointer to the source file
 * @param[in]	strFileName	The new filename
 *
 * @return The result of the duplication of the file
 *
 * @todo on error delete file?
 */
bool DuplicateFile(FILE *lpFile, std::string &strFileName)
{
	size_t	ulReadsize = 0;
	std::unique_ptr<char[]> lpBuffer;

	// create new file
	std::unique_ptr<FILE, file_deleter> pfNew(fopen(strFileName.c_str(), "wb"));
	if(pfNew == NULL) {
		ec_log_err("Unable to create file %s: %s", strFileName.c_str(), strerror(errno));
		return false;
	}

	// Set file pointer at the begin.
	rewind(lpFile);
	lpBuffer.reset(new(std::nothrow) char[BLOCKSIZE]);
	if (!lpBuffer) {
		ec_log_crit("DuplicateFile is out of memory");
		return false;
	}

	// FIXME use splice
	while (!feof(lpFile)) {
		ulReadsize = fread(lpBuffer.get(), 1, BLOCKSIZE, lpFile);
		if (ferror(lpFile)) {
			ec_log_crit("DuplicateFile: fread: %s", strerror(errno));
			return false;
		}
		if (fwrite(lpBuffer.get(), 1, ulReadsize, pfNew.get()) != ulReadsize) {
			ec_log_crit("Error during write to \"%s\": %s", strFileName.c_str(), strerror(errno));
			return false;
		}
	}
	return true;
}

} /* namespace */
