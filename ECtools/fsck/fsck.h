/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef KOPANO_FSCK
#define KOPANO_FSCK

#include <kopano/platform.h>
#include <string>
#include <list>
#include <set>
#include <mapidefs.h>

/*
 * Global configuration
 */
extern std::string auto_fix;
extern std::string auto_del;

class Fsck {
private:
	ULONG ulFolders = 0, ulEntries = 0, ulProblems = 0;
	ULONG ulFixed = 0, ulDeleted = 0;
	virtual HRESULT ValidateItem(LPMESSAGE lpMessage, const std::string &strClass) = 0;

public:
	virtual ~Fsck(void) = default;
	HRESULT ValidateMessage(LPMESSAGE lpMessage, const std::string &strName, const std::string &strClass);
	HRESULT ValidateFolder(LPMAPIFOLDER lpFolder, const std::string &strName);
	HRESULT AddMissingProperty(LPMESSAGE lpMessage, const std::string &strName, ULONG ulTag, __UPV Value);
	HRESULT ReplaceProperty(LPMESSAGE lpMessage, const std::string &strName, ULONG ulTag, const std::string &strError, __UPV Value);
	HRESULT DeleteRecipientList(LPMESSAGE lpMessage, std::list<unsigned int> &mapiReciptDel, bool &bChanged);
	HRESULT DeleteMessage(LPMAPIFOLDER folder, const SPropValue *prop);
	HRESULT ValidateRecursiveDuplicateRecipients(LPMESSAGE lpMessage, bool &bChanged);
	HRESULT ValidateDuplicateRecipients(LPMESSAGE lpMessage, bool &bChanged);
	void PrintStatistics(const std::string &title);
};

class FsckCalendar final : public Fsck {
private:
	HRESULT ValidateItem(IMessage *, const std::string &cls) override;
	HRESULT ValidateMinimalNamedFields(LPMESSAGE lpMessage);
	HRESULT ValidateTimestamps(LPMESSAGE lpMessage);
	HRESULT ValidateRecurrence(LPMESSAGE lpMessage);
};

class FsckContact final : public Fsck {
private:
	HRESULT ValidateItem(IMessage *, const std::string &cls) override;
	HRESULT ValidateContactNames(LPMESSAGE lpMessage);
};

class FsckTask final : public Fsck {
private:
	HRESULT ValidateItem(IMessage *, const std::string &cls) override;
	HRESULT ValidateMinimalNamedFields(LPMESSAGE lpMessage);
	HRESULT ValidateTimestamps(LPMESSAGE lpMessage);
	HRESULT ValidateCompletion(LPMESSAGE lpMessage);
};

/*
 * Helper functions.
 */
HRESULT allocNamedIdList(ULONG ulSize, LPMAPINAMEID **lpppNameArray);
HRESULT ReadProperties(IMessage *, ULONG count, const ULONG *tags, SPropValue **out);
HRESULT ReadNamedProperties(LPMESSAGE lpMessage, ULONG ulCount,
			    LPMAPINAMEID *lppTag,
			    LPSPropTagArray *lppPropertyTagArray,
			    LPSPropValue *lppPropertyArray);

#endif /* KOPANO_FSCK */
