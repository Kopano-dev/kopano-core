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
#include "ECConfigImpl.h"
#include <kopano/charset/convert.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static const char THIS_FILE[] = __FILE__;
#endif

ECConfig *ECConfig::Create(const configsetting_t *lpDefaults,
    const char *const *lpszDirectives)
{
	return new ECConfigImpl(lpDefaults, lpszDirectives);
}

bool ECConfig::LoadSettings(const wchar_t *szFilename)
{
	convert_context converter;
	return LoadSettings(converter.convert_to<char*>(szFilename));
}


/**
 * Get the default path for the configuration file specified with lpszBasename.
 * Usually this will return '/etc/kopano/<lpszBasename>'. However, the path to
 * the configuration files can be altered by setting the 'KOPANO_CONFIG_PATH'
 * environment variable.
 *
 * @param[in]	lpszBasename
 * 						The basename of the requested configuration file. Passing
 * 						NULL or an empty string will result in the default path
 * 						to be returned.
 * 
 * @returns		The full path to the requested configuration file. Memory for
 * 				the returned data is allocated in this function and will be freed
 * 				at program termination.
 *
 * @warning This function is not thread safe!
 */
const char* ECConfig::GetDefaultPath(const char* lpszBasename)
{
	typedef map<string, string> stringmap_t;
	typedef stringmap_t::iterator iterator_t;
	typedef pair<iterator_t, bool> insertresult_t;

	// @todo: Check how this behaves with dlopen,dlclose,dlopen,etc...
	// We use a static map here to store the strings we're going to return.
	// This could have been a global, but this way everything is kept together.
	static stringmap_t s_mapPaths;

	if (!lpszBasename)
		lpszBasename = "";

	insertresult_t result = s_mapPaths.insert(stringmap_t::value_type(lpszBasename, string()));
	if (result.second == true) {		// New item added, so create the actual path
		const char *lpszDirname = getenv("KOPANO_CONFIG_PATH");
		if (!lpszDirname || lpszDirname[0] == '\0')
			lpszDirname = "/etc/kopano";
		result.first->second = string(lpszDirname) + "/" + lpszBasename;
	}
	return result.first->second.c_str();
}
