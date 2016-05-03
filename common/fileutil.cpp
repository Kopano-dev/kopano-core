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
#include <string>
#include <cerrno>
#include <cstring>
#include <kopano/ECIConv.h>
#include <kopano/ECLogger.h>

#include <mapicode.h> // only for MAPI error codes
#include <mapidefs.h> // only for MAPI error codes

#ifdef LINUX
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#endif
#include "fileutil.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define BLOCKSIZE	65536

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
	size_t	sizebufferout, readsize;
	FILE*	fTmp = NULL;

	if(fin == NULL || fout == NULL)
		return MAPI_E_INVALID_PARAMETER;

	fTmp = tmpfile();
	if(fTmp == NULL) {
		perror("Unable to create tmp file");
		return MAPI_E_CALL_FAILED;
	}

	while (!feof(fin)) {
		readsize = fread(bufferin, 1, BLOCKSIZE / 2, fin);
		if (ferror(fin)) {
			perror("Read error");//FIXME: What an error?, what now?
			fclose(fTmp);
			return MAPI_E_CORRUPT_DATA;
		}

		BufferLFtoCRLF(readsize, bufferin, bufferout, &sizebufferout);

		if (fwrite(bufferout, 1, sizebufferout, fTmp) != sizebufferout) {
			perror("Write error");//FIXME: What an error?, what now?
			fclose(fTmp);
			return MAPI_E_CORRUPT_DATA;
		}
	}

	*fout = fTmp;
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
HRESULT HrMapFileToBuffer(FILE *f, char **lppBuffer, int *lpSize, bool *lpImmap)
{
	char *lpBuffer = NULL;
	int offset = 0;
	long ulBufferSize = BLOCKSIZE;
	long ulReadsize;
	struct stat stat;
	int fd = fileno(f);

	*lpImmap = false;

#ifdef LINUX
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
#endif /* LINUX */

	/* mmap failed (probably reading from STDIN as a stream), just read the file into memory, and return that */
	lpBuffer = (char*)malloc(BLOCKSIZE); // will be deleted as soon as possible
	while (!feof(f)) {
		ulReadsize = fread(lpBuffer+offset, 1, BLOCKSIZE, f);
		if (ferror(f)) {
			perror("Read error");
			break;
		}

		offset += ulReadsize;
		if (offset + BLOCKSIZE > ulBufferSize) {    // Next read could cross buffer boundary, realloc
			char *lpRealloc = (char*)realloc(lpBuffer, offset + BLOCKSIZE);
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
	} else {
		/* Add terminate character */
		lpBuffer[offset] = 0;

		*lppBuffer = lpBuffer;
		*lpSize = offset;
	}
	return hrSuccess;
}

/** 
 * Free a buffer from HrMapFileToBuffer
 * 
 * @param[in] lpBuffer buffer to free
 * @param[in] ulSize size of the buffer
 * @param[in] bImmap marker if the buffer is mapped or not
 */
HRESULT HrUnmapFileBuffer(char *lpBuffer, int ulSize, bool bImmap)
{
#ifdef LINUX
	if (bImmap)
		munmap(lpBuffer, mmapsize(ulSize));
	else
#endif
		free(lpBuffer);

	return hrSuccess;
}

/** 
 * Reads a file into an std::string using file mapping if possible.
 *
 * @todo doesn't the std::string undermine the whole idea of mapping?
 * @todo std::string has a length method, so what's with the lpSize parameter?
 * 
 * @param[in] f file to read in memory
 * @param[out] lpstrBuffer string containing the file contents, optionally returned (why?)
 * @param[out] lpSize size of the buffer, optionally returned
 * 
 * @return 
 */
HRESULT HrMapFileToString(FILE *f, std::string *lpstrBuffer, int *lpSize)
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
	if (lpSize)
		*lpSize = ulBufferSize;

exit:
	if (lpBuffer)
		HrUnmapFileBuffer(lpBuffer, ulBufferSize, immap);

	return hr;
}

/**
 * Duplicate a file, to a given location
 *
 * @param[in]	lpFile Pointer to the source file
 * @param[in]	strFileName	The new file name
 *
 * @return The result of the duplication of the file
 *
 * @todo on error delete file?
 */
bool DuplicateFile(FILE *lpFile, std::string &strFileName)
{
	bool bResult = true;
	size_t	ulReadsize = 0;
	FILE *pfNew = NULL;
	char *lpBuffer = NULL;

	// create new file
	pfNew = fopen(strFileName.c_str(), "wb");
	if(pfNew == NULL) {
		ec_log_err("Unable to create file %s: %s", strFileName.c_str(), strerror(errno));
		bResult = false;
		goto exit;
	}

	// Set file pointer at the begin.
	rewind(lpFile);

	lpBuffer = (char*)malloc(BLOCKSIZE); 
	if (!lpBuffer) {
		ec_log_crit("DuplicateFile is out of memory");

		bResult = false;
		goto exit;
	}

	// FIXME use splice
	while (!feof(lpFile)) {
		ulReadsize = fread(lpBuffer, 1, BLOCKSIZE, lpFile);
		if (ferror(lpFile)) {
			ec_log_crit("DuplicateFile: fread: %s", strerror(errno));
			bResult = false;
			goto exit;
		}
		

		if (fwrite(lpBuffer, 1, ulReadsize , pfNew) != ulReadsize) {
			ec_log_crit("Error during write to \"%s\": %s", strFileName.c_str(), strerror(errno));
			bResult = false;
			goto exit;
		}
	}

exit:
	free(lpBuffer);
	if (pfNew)
		fclose(pfNew);

	return bResult;
}

/**
 * Convert file from UCS2 to UTF8
 *
 * @param[in] strSrcFileName Source filename
 * @param[in] strDstFileName Destination filename
 */
bool ConvertFileFromUCS2ToUTF8(const std::string &strSrcFileName, const std::string &strDstFileName)
{
	bool bResult = false;
	int ulBufferSize = 0;
	FILE *pfSrc = NULL;
	FILE *pfDst = NULL;
	char *lpBuffer = NULL;
	bool bImmap = false;
	std::string strConverted;

	pfSrc = fopen(strSrcFileName.c_str(), "rb");
	if(pfSrc == NULL) {
		ec_log_err("%s: Unable to open file \"%s\": %s", __PRETTY_FUNCTION__, strSrcFileName.c_str(), strerror(errno));
		goto exit;
	}

	// create new file
	pfDst = fopen(strDstFileName.c_str(), "wb");
	if(pfDst == NULL) {
		ec_log_err("%s: Unable to create file \"%s\": %s", __PRETTY_FUNCTION__, strDstFileName.c_str(), strerror(errno));
		goto exit;
	}

	if(HrMapFileToBuffer(pfSrc, &lpBuffer, &ulBufferSize, &bImmap))
		goto exit;

	try {
		strConverted = convert_to<std::string>("UTF-8", lpBuffer, ulBufferSize, "UCS-2//IGNORE");
	} catch (const std::runtime_error &) {
		goto exit;
	}
	
	if (fwrite(strConverted.c_str(), 1, strConverted.size(), pfDst) != strConverted.size()) { 
		ec_log_crit("%s: Unable to write to file \"%s\": %s", __PRETTY_FUNCTION__, strDstFileName.c_str(), strerror(errno));
		goto exit;
	}

	bResult = true;

exit:
	if (lpBuffer)
		HrUnmapFileBuffer(lpBuffer, ulBufferSize, bImmap);
	
	if (pfSrc)
		fclose(pfSrc);

	if (pfDst)
		fclose(pfDst);

	return bResult;
}
