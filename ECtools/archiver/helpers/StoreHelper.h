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

#ifndef STOREHELPER_H_INCLUDED
#define STOREHELPER_H_INCLUDED

#include <memory>
#include <kopano/zcdefs.h>
#include "MAPIPropHelper.h"

class ECRestriction;
class ECAndRestriction;
class ECOrRestriction;

namespace za { namespace helpers {

class StoreHelper;
typedef std::unique_ptr<StoreHelper> StoreHelperPtr;

/**
 * The StoreHelper class provides some common utility functions that relate to IMsgStore
 * objects in the archiver context.
 */
class StoreHelper _kc_final : public MAPIPropHelper {
public:
	static HRESULT Create(MsgStorePtr &ptrMsgStore, StoreHelperPtr *lpptrStoreHelper);
	
	HRESULT GetFolder(const tstring &strFolder, bool bCreate, LPMAPIFOLDER *lppFolder);
	HRESULT UpdateSearchFolders();
	
	HRESULT GetIpmSubtree(LPMAPIFOLDER *lppFolder);
	HRESULT GetSearchFolders(LPMAPIFOLDER *lppSearchArchiveFolder, LPMAPIFOLDER *lppSearchDeleteFolder, LPMAPIFOLDER *lppSearchStubFolder);
	
private:
	StoreHelper(MsgStorePtr &ptrMsgStore);
	HRESULT Init();
	
	HRESULT GetSubFolder(MAPIFolderPtr &ptrFolder, const tstring &strFolder, bool bCreate, LPMAPIFOLDER *lppFolder);

	enum eSearchFolder {esfArchive = 0, esfDelete, esfStub, esfMax};
	
	HRESULT CheckAndUpdateSearchFolder(LPMAPIFOLDER lpSearchFolder, eSearchFolder esfWhich);
	HRESULT CreateSearchFolder(eSearchFolder esfWhich, LPMAPIFOLDER *lppSearchFolder);
	HRESULT CreateSearchFolders(LPMAPIFOLDER *lppSearchArchiveFolder, LPMAPIFOLDER *lppSearchDeleteFolder, LPMAPIFOLDER *lppSearchStubFolder);
	HRESULT DoCreateSearchFolder(LPMAPIFOLDER lpParent, eSearchFolder esfWhich, LPMAPIFOLDER *lppSearchFolder);

	HRESULT SetupSearchArchiveFolder(LPMAPIFOLDER lpSearchFolder, const ECRestriction *lpresClassCheck, const ECRestriction *lpresArchiveCheck);
	HRESULT SetupSearchDeleteFolder(LPMAPIFOLDER lpSearchFolder, const ECRestriction *lpresClassCheck, const ECRestriction *lpresArchiveCheck);
	HRESULT SetupSearchStubFolder(LPMAPIFOLDER lpSearchFolder, const ECRestriction *lpresClassCheck, const ECRestriction *lpresArchiveCheck);

	HRESULT GetClassCheckRestriction(ECOrRestriction *lpresClassCheck);
	HRESULT GetArchiveCheckRestriction(ECAndRestriction *lpresArchiveCheck);

private:
	typedef HRESULT(StoreHelper::*fn_setup_t)(LPMAPIFOLDER, const ECRestriction *, const ECRestriction *);
	struct search_folder_info_t {
		LPCTSTR		lpszName;
		LPCTSTR		lpszDescription;
		fn_setup_t	fnSetup;
	};

	static search_folder_info_t s_infoSearchFolders[];

	MsgStorePtr	m_ptrMsgStore;
	MAPIFolderPtr m_ptrIpmSubtree;
	
	PROPMAP_START
	PROPMAP_DEF_NAMED_ID(ARCHIVE_STORE_ENTRYIDS)
	PROPMAP_DEF_NAMED_ID(ARCHIVE_ITEM_ENTRYIDS)
	PROPMAP_DEF_NAMED_ID(ORIGINAL_SOURCEKEY)
	PROPMAP_DEF_NAMED_ID(SEARCH_FOLDER_ENTRYIDS)
	PROPMAP_DEF_NAMED_ID(STUBBED)
	PROPMAP_DEF_NAMED_ID(DIRTY)
	PROPMAP_DEF_NAMED_ID(FLAGS)
	PROPMAP_DEF_NAMED_ID(VERSION)
};

}} // namespaces

#endif // !defined STOREHELPER_H_INCLUDED
