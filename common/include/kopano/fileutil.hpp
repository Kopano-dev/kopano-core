/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef EC_FILEUTIL_HPP
#define EC_FILEUTIL_HPP 1

#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <string>
#include <cstdio>

namespace KC {

class ECConfig;

class KC_EXPORT TmpPath KC_FINAL {
	private:
	std::string path;

	public:
	KC_HIDDEN TmpPath();
	bool OverridePath(ECConfig *const ec);
	KC_HIDDEN const std::string &getTempPath() const { return path; }
	static TmpPath instance;
};

class file_deleter {
	public:
	void operator()(FILE *f) { fclose(f); }
};

extern KC_EXPORT HRESULT HrFileLFtoCRLF(FILE *fin, FILE **fout);
extern KC_EXPORT HRESULT HrMapFileToString(FILE *f, std::string *buf);
extern KC_EXPORT bool DuplicateFile(FILE *, std::string &newname);
extern KC_EXPORT int CreatePath(std::string, unsigned int = 0770);
extern KC_EXPORT ssize_t read_retry(int, void *, size_t);
extern KC_EXPORT ssize_t write_retry(int, const void *, size_t);
extern KC_EXPORT bool force_buffers_to_disk(int fd);

} /* namespace */

#endif
