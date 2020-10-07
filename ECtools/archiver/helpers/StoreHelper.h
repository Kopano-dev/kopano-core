/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <memory>
#include <kopano/zcdefs.h>
#include <kopano/memory.hpp>
#include "MAPIPropHelper.h"

namespace KC {

class Restriction;
class ECAndRestriction;
class ECOrRestriction;

namespace helpers {

/**
 * The StoreHelper class provides some common utility functions that relate to IMsgStore
 * objects in the archiver context.
 */
class KC_EXPORT StoreHelper final : public MAPIPropHelper {
public:
	static HRESULT Create(IMsgStore *, std::unique_ptr<StoreHelper> *);
	KC_HIDDEN HRESULT GetFolder(const tstring &name, bool create, IMAPIFolder **ret);
	KC_HIDDEN HRESULT UpdateSearchFolders();
	KC_HIDDEN HRESULT GetIpmSubtree(IMAPIFolder **);
	HRESULT GetSearchFolders(LPMAPIFOLDER *lppSearchArchiveFolder, LPMAPIFOLDER *lppSearchDeleteFolder, LPMAPIFOLDER *lppSearchStubFolder);

private:
	KC_HIDDEN StoreHelper(IMsgStore *);
	KC_HIDDEN HRESULT Init();
	KC_HIDDEN HRESULT GetSubFolder(IMAPIFolder *, const tstring &name, bool create, IMAPIFolder **ret);
	enum eSearchFolder {esfArchive = 0, esfDelete, esfStub, esfMax};
	KC_HIDDEN HRESULT CheckAndUpdateSearchFolder(IMAPIFolder *, eSearchFolder which);
	KC_HIDDEN HRESULT CreateSearchFolder(eSearchFolder which, IMAPIFolder **);
	KC_HIDDEN HRESULT CreateSearchFolders(IMAPIFolder **archive_folder, IMAPIFolder **delete_folder, IMAPIFolder **stub_folder);
	KC_HIDDEN HRESULT DoCreateSearchFolder(IMAPIFolder *parent, eSearchFolder which, IMAPIFolder **retsf);
	KC_HIDDEN HRESULT SetupSearchArchiveFolder(IMAPIFolder *folder, const Restriction *class_chk, const Restriction *arc_chk);
	KC_HIDDEN HRESULT SetupSearchDeleteFolder(IMAPIFolder *folder, const Restriction *class_chk, const Restriction *arc_chk);
	KC_HIDDEN HRESULT SetupSearchStubFolder(IMAPIFolder *folder, const Restriction *class_chk, const Restriction *arc_chk);
	KC_HIDDEN HRESULT GetClassCheckRestriction(ECOrRestriction *class_chk);
	KC_HIDDEN HRESULT GetArchiveCheckRestriction(ECAndRestriction *arc_chk);

	typedef HRESULT(StoreHelper::*fn_setup_t)(LPMAPIFOLDER, const Restriction *, const Restriction *);
	struct search_folder_info_t {
		const TCHAR *lpszName, *lpszDescription;
		fn_setup_t	fnSetup;
	};

	static const search_folder_info_t s_infoSearchFolders[];
	object_ptr<IMsgStore> m_ptrMsgStore;
	object_ptr<IMAPIFolder> m_ptrIpmSubtree;

	PROPMAP_DECL()
	PROPMAP_DEF_NAMED_ID(ARCHIVE_STORE_ENTRYIDS)
	PROPMAP_DEF_NAMED_ID(ARCHIVE_ITEM_ENTRYIDS)
	PROPMAP_DEF_NAMED_ID(ORIGINAL_SOURCEKEY)
	PROPMAP_DEF_NAMED_ID(SEARCH_FOLDER_ENTRYIDS)
	PROPMAP_DEF_NAMED_ID(STUBBED)
	PROPMAP_DEF_NAMED_ID(DIRTY)
	PROPMAP_DEF_NAMED_ID(FLAGS)
	PROPMAP_DEF_NAMED_ID(VERSION)
};

}} /* namespace */
