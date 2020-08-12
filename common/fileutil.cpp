/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <kopano/stringutil.h>
#include <kopano/charset/convert.h>
#include <memory>
#include <new>
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
#include <dirent.h>
#include <unistd.h>
#include <kopano/ECConfig.h>
#include <kopano/UnixUtil.h>
#include <kopano/fileutil.hpp>
#define BLOCKSIZE	65536

using namespace std::string_literals;

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
		ec_log_err("Unable to create tmp file: %s", strerror(errno));
		return MAPI_E_CALL_FAILED;
	}

	while (!feof(fin)) {
		size_t readsize = fread(bufferin, 1, BLOCKSIZE / 2, fin);
		if (ferror(fin)) {
			ec_log_err("%s/fread: %s", __func__, strerror(errno));
			return MAPI_E_CORRUPT_DATA;
		}

		BufferLFtoCRLF(readsize, bufferin, bufferout, &sizebufferout);
		if (fwrite(bufferout, 1, sizebufferout, fTmp.get()) != sizebufferout) {
			ec_log_err("%s/fwrite: %s", __func__, strerror(errno));
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
	lpstrBuffer->clear();
	char buf[BLOCKSIZE];
	while (!feof(f)) {
		auto rd = fread(buf, 1, sizeof(buf), f);
		if (ferror(f)) {
			ec_log_err("MapFileToString/fread: %s", strerror(errno));
			return MAPI_E_CORRUPT_DATA;
		}
		try {
			lpstrBuffer->append(buf, rd);
		} catch (const std::bad_alloc &) {
			ec_log_err("MapFileToString/malloc: %s", strerror(errno));
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

/* Does mkdir -p <path> */
int CreatePath(std::string s, unsigned int mode)
{
	if (s.size() == 0)
		return -ENOENT;
	size_t p = 0;
	while (s[p] == '/')
		/* No need to create the root directory; it always exists. */
		++p;
	do {
		p = s.find('/', p);
		if (p == std::string::npos)
			break;
		s[p] = '\0';
		auto ret = mkdir(s.c_str(), mode);
		if (ret != 0 && errno != EEXIST)
			return -errno;
		s[p++] = '/';
		while (s[p] == '/')
			++p;
	} while (true);
	auto ret = mkdir(s.c_str(), mode);
	if (ret != 0 && errno != EEXIST)
		return -errno;
	return 0;
}

ssize_t read_retry(int fd, void *data, size_t len)
{
	auto buf = static_cast<char *>(data);
	size_t tread = 0;

	while (len > 0) {
		ssize_t ret = read(fd, buf, len);
		if (ret < 0 && (errno == EINTR || errno == EAGAIN))
			continue;
		if (ret < 0)
			return ret;
		if (ret == 0)
			break;
		len -= ret;
		buf += ret;
		tread += ret;
	}
	return tread;
}

ssize_t write_retry(int fd, const void *data, size_t len)
{
	auto buf = static_cast<const char *>(data);
	size_t twrote = 0;

	while (len > 0) {
		ssize_t ret = write(fd, buf, len);
		if (ret < 0 && (errno == EINTR || errno == EAGAIN))
			continue;
		if (ret < 0)
			return ret;
		if (ret == 0)
			break;
		len -= ret;
		buf += ret;
		twrote += ret;
	}
	return twrote;
}

bool force_buffers_to_disk(const int fd)
{
	return fsync(fd) != -1;
}

static bool dexec_ignore_name(const char *n)
{
	size_t len = strlen(n);
	if (len >= 1 && n[0] == '#')
		return true;
	if (len >= 1 && n[len-1] == '~')
		return true;
	if (len >= 4 && strcmp(&n[len-4], ".bak") == 0)
		return true;
	if (len >= 4 && strcmp(&n[len-4], ".old") == 0)
		return true;
	if (len >= 7 && strcmp(&n[len-7], ".rpmnew") == 0)
		return true;
	if (len >= 8 && strcmp(&n[len-8], ".rpmorig") == 0)
		return true;
	return false;
}

/**
 * Evaluates programs from a set of directories using the ".d"-style mechanism for
 * overrides and masking.
 */
dexec_state dexec_scan(const std::vector<std::string> &dirlist)
{
	dexec_state st;
	for (const auto &dir : dirlist) {
		std::unique_ptr<DIR, KC::fs_deleter> dh(opendir(dir.c_str()));
		if (dh == nullptr) {
			if (errno != ENOENT)
				st.warnings.push_back(format("Could not read %s: %s", dir.c_str(), strerror(errno)));
			continue;
		}
		struct dirent *de;
		while ((de = readdir(dh.get())) != nullptr) {
			if (dexec_ignore_name(de->d_name)) {
				st.warnings.push_back(format("Ignoring \"%s/%s\"", dir.c_str(), de->d_name));
				continue;
			}
			struct stat sb;
			auto ret = fstatat(dirfd(dh.get()), de->d_name, &sb, 0);
			if (ret < 0)
				continue;
			if (S_ISREG(sb.st_mode))
				st.prog[de->d_name] = dir + "/"s + de->d_name;
			else
				st.prog.erase(de->d_name);
		}
	}
	return st;
}

} /* namespace */
