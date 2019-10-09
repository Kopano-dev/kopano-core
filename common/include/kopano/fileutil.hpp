/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef _FILEUTIL_H
#define _FILEUTIL_H

#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <string>
#include <cstdio>

namespace KC {

class ECConfig;

class _kc_export TmpPath _kc_final {
	private:
	std::string path;

	public:
	_kc_hidden TmpPath(void);
	bool OverridePath(ECConfig *const ec);
	_kc_hidden const std::string &getTempPath(void) const { return path; }
	static _kc_export TmpPath instance;
};

class file_deleter {
	public:
	void operator()(FILE *f) { fclose(f); }
};

extern _kc_export HRESULT HrFileLFtoCRLF(FILE *fin, FILE **fout);
extern _kc_export HRESULT HrMapFileToString(FILE *f, std::string *buf);
extern _kc_export bool DuplicateFile(FILE *, std::string &newname);
extern _kc_export int CreatePath(std::string, unsigned int = 0770);
extern _kc_export ssize_t read_retry(int, void *, size_t);
extern _kc_export ssize_t write_retry(int, const void *, size_t);
extern _kc_export bool force_buffers_to_disk(int fd);

} /* namespace */

#endif
