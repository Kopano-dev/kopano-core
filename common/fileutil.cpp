/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#include <kopano/platform.h>
#include <kopano/stringutil.h>
#include <kopano/charset/convert.h>
#include <memory>
#include <string>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <kopano/ECLogger.h>

#include <mapicode.h>
#include <mapidefs.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <kopano/ECConfig.h>
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
	char bufferin[BLOCKSIZE/2], bufferout[BLOCKSIZE+1];
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
 * Reads a file into a std::string
 *
 * @param[in] f file to read in memory
 * @param[out] lpstrBuffer string containing the file contents
 * 
 * @return 
 */
HRESULT HrMapFileToString(FILE *f, std::string *lpstrBuffer)
{
	struct stat sb;
	if (fstat(fileno(f), &sb) != 0)
		return MAPI_E_CALL_FAILED;
	lpstrBuffer->clear();
	char buf[BLOCKSIZE];
	while (!feof(f)) {
		auto rd = fread(buf, 1, sizeof(buf), f);
		if (ferror(f)) {
			perror("MapFileToString/fread");
			return MAPI_E_CORRUPT_DATA;
		}
		try {
			lpstrBuffer->append(buf, rd);
		} catch (const std::bad_alloc &) {
			perror("malloc");
			return MAPI_E_NOT_ENOUGH_MEMORY;
		}
	}
	return hrSuccess;
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

TmpPath TmpPath::instance;

TmpPath::TmpPath()
{
	const char *dummy = nullptr;

	if (path.empty()) {
		dummy = getenv("TMP");
		if (dummy != nullptr)
			path = dummy;
	}
	if (path.empty()) {
		dummy = getenv("TEMP");
		if (dummy != nullptr)
			path = dummy;
	}
	if (!path.empty()) {
		struct stat st;
		if (stat(path.c_str(), &st) == -1)
			path = "/tmp"; // what to do if we can't access that path either? FIXME
	}
	if (path.empty())
		path = "/tmp";
}

bool TmpPath::OverridePath(ECConfig *ec)
{
	bool rc = true;
	const char *newPath = ec->GetSetting("tmp_path");

	if (newPath == nullptr || newPath[0] == '\0')
		return true;
	path = newPath;
	size_t s = path.size();
	if (path.at(s - 1) == '/' && s > 1)
		path = path.substr(0, s - 1);
	struct stat st;
	if (stat(path.c_str(), &st) == -1) {
		path = "/tmp"; // what to do if we can't access that path either? FIXME
		rc = false;
	}
	setenv("TMP", newPath, 1);
	setenv("TEMP", newPath, 1);
	return rc;
}

} /* namespace */
