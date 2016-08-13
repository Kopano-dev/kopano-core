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

#ifndef _FILEUTIL_H
#define _FILEUTIL_H

#include <string>

HRESULT HrFileLFtoCRLF(FILE *fin, FILE** fout);
HRESULT HrMapFileToString(FILE *f, std::string *lpstrBuffer, int *lpSize = NULL);
HRESULT HrMapFileToBuffer(FILE *f, char **lppBuffer, int *lpSize, bool *lpImmap);
HRESULT HrUnmapFileBuffer(char *lpBuffer, int ulSize, bool bImmap);

bool DuplicateFile(FILE *lpFile, std::string &strFileName);

#endif
