/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef operations_INCLUDED
#define operations_INCLUDED

#include <memory>
#include "operations_fwd.h"
#include <mapix.h>
#include <kopano/mapi_ptr.h>

namespace KC {

class ECArchiverLogger;

namespace operations {

/**
 * IArchiveOperation specifies the interface to all archiver operations.
 */
class IArchiveOperation {
public:
	/**
	 * virtual destructor.
	 */
	virtual ~IArchiveOperation(void) = default;

	/**
	 * Entrypoint for all archive operations.
	 *
	 * @param[in]	lpFolder
	 *					A MAPIFolder pointer that points to the folder that's to be processed. This folder will usually be
	 *					a searchfolder containing the messages that are ready to be processed.
	 * @param[in]	cProps
	 *					The number op properties pointed to by lpProps.
	 * @param[in]	lpProps
	 *					Pointer to an array of properties that are used by the Operation object.
	 */
	virtual HRESULT ProcessEntry(IMAPIFolder *, const SRow &proprow) = 0;

	/**
	 * Obtain the restriction the messages need to match in order to be processed by this operation.
	 *
	 * @param[in]	LPMAPIPROP			A MAPIProp object that's used to resolve named properties on.
	 * @param[out]	lppRestriction		The restriction to be matched.
	 */
	virtual HRESULT GetRestriction(LPMAPIPROP LPMAPIPROP, LPSRestriction *lppRestriction) = 0;

	/**
	 * Verify the message to make sure a message isn't processed while it shouldn't caused
	 * by searchfolder lags.
	 *
	 * @param[in]	lpMessage		The message to verify
	 *
	 * @return	hrSuccess			The message is verified to be valid for the operation.
	 * @return	MAPI_E_NOT_FOUND	The message should not be processed.
	 */
	virtual HRESULT VerifyRestriction(LPMESSAGE lpMessage) = 0;
};


/**
 * This class contains some basic functionality for all operations.
 *
 * It generates the restriction.
 * It contains the logger.
 */
class ArchiveOperationBase : public IArchiveOperation {
public:
	ArchiveOperationBase(std::shared_ptr<ECArchiverLogger>, int ulAge, bool bProcessUnread, ULONG ulInhibitMask);
	HRESULT GetRestriction(IMAPIProp *, SRestriction **) override;
	HRESULT VerifyRestriction(IMessage *) override;

protected:
	std::shared_ptr<ECArchiverLogger> Logger() const { return m_lpLogger; }

private:
	std::shared_ptr<ECArchiverLogger> m_lpLogger;
	const int m_ulAge;
	const bool m_bProcessUnread;
	const ULONG m_ulInhibitMask;
	FILETIME m_ftCurrent;
};


/**
 * ArchiveOperationBaseEx is the base implementation of the IArchiveOperation interface. It's main functionality
 * is iterating through all the items in the folder passed to ProcessEntry.
 */
class ArchiveOperationBaseEx : public ArchiveOperationBase {
public:
	ArchiveOperationBaseEx(std::shared_ptr<ECArchiverLogger>, int ulAge, bool bProcessUnread, ULONG ulInhibitMask);
	HRESULT ProcessEntry(IMAPIFolder *, const SRow &proprow) override;

protected:
	/**
	 * Returns a pointer to a MAPIFolderPtr, which references the current working folder.
	 * @return A reference to a MAPIFolderPtr.
	 */
	MAPIFolderPtr& CurrentFolder() { return m_ptrCurFolder; }

private:
	/**
	 * Called by ProcessEntry after switching source folders. Derived classes will need to
	 * implement this method. It can be used to perform operations that only need to be done when
	 * the source folder is switched.
	 *
	 * @param[in]	lpFolder	The just opened folder.
	 */
	virtual HRESULT EnterFolder(LPMAPIFOLDER lpFolder) = 0;

	/**
	 * Called by ProcessEntry before switching source folders. Derived classes will need to
	 * implement this method. It can be used to perform operations that only need to be done when
	 * the source folder is switched.
	 */
	virtual HRESULT LeaveFolder() = 0;

	/**
	 * Called by ProcessEntry for each message found in the search folder that also matches the age restriction.
	 *
	 * @param[in]	cProps
	 *					The number op properties pointed to by lpProps.
	 * @param[in]	proprow
	 *					A list of properties that are used by the Operation object.
	 */
	virtual HRESULT DoProcessEntry(const SRow &proprow) = 0;

	SPropValuePtr m_ptrCurFolderEntryId;
	MAPIFolderPtr m_ptrCurFolder;
};

}} /* namespace */

#endif // ndef operations_INCLUDED
