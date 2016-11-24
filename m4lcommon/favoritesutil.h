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

#ifndef FAVORITESUTIL_H
#define FAVORITESUTIL_H

#include <kopano/zcdefs.h>
#include <mapix.h>

namespace KC {

#define FAVO_FOLDER_LEVEL_BASE		0x00000
#define FAVO_FOLDER_LEVEL_ONE		0x00001
#define FAVO_FOLDER_LEVEL_SUB		0x00002

#define FAVO_FOLDER_INHERIT_AUTO	0x10000 //unused

// Default column set for shortcut folder (favorites)
enum {
	SC_INSTANCE_KEY,
	SC_FAV_PUBLIC_SOURCE_KEY,
	SC_FAV_PARENT_SOURCE_KEY,
	SC_FAV_DISPLAY_NAME, 
	SC_FAV_DISPLAY_ALIAS,
	SC_FAV_LEVEL_MASK,
	SC_FAV_CONTAINER_CLASS,
	SHORTCUT_NUM
};

extern _kc_export LPSPropTagArray GetShortCutTagArray(void);

HRESULT AddToFavorite(IMAPIFolder *lpShortcutFolder, ULONG ulLevel, LPCTSTR lpszAliasName, ULONG ulFlags, ULONG cValues, LPSPropValue lpPropArray);
extern _kc_export HRESULT GetShortcutFolder(LPMAPISESSION, LPTSTR folder_name, LPTSTR folder_comment, ULONG flags, LPMAPIFOLDER *scfolder);
HRESULT CreateShortcutFolder(IMsgStore *lpMsgStore, LPTSTR lpszFolderName, LPTSTR lpszFolderComment, ULONG ulFlags, LPMAPIFOLDER* lppShortcutFolder);
extern _kc_export HRESULT DelFavoriteFolder(IMAPIFolder *scfolder, LPSPropValue source_key);
extern _kc_export HRESULT AddFavoriteFolder(IMAPIFolder *scfolder, LPMAPIFOLDER folder, LPCTSTR alias_name, ULONG flags);

} /* namespace */

#endif //#ifndef FAVORITESUTIL_H
