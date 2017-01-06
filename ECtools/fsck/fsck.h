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

#ifndef KOPANO_FSCK
#define KOPANO_FSCK

#include <kopano/platform.h>
#include <string>
#include <list>
#include <set>
using namespace std;

#include <kopano/zcdefs.h>
#include <mapidefs.h>

/*
 * Global configuration
 */
extern string auto_fix;
extern string auto_del;

class Fsck {
private:
	ULONG ulFolders = 0, ulEntries = 0, ulProblems = 0;
	ULONG ulFixed = 0, ulDeleted = 0;
	virtual HRESULT ValidateItem(LPMESSAGE lpMessage, const std::string &strClass) = 0;

public:
	virtual ~Fsck(void) _kc_impdtor;
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

class FsckCalendar _kc_final : public Fsck {
private:
	HRESULT ValidateItem(LPMESSAGE lpMessage, const std::string &strClass) _kc_override;
	HRESULT ValidateMinimalNamedFields(LPMESSAGE lpMessage);
	HRESULT ValidateTimestamps(LPMESSAGE lpMessage);
	HRESULT ValidateRecurrence(LPMESSAGE lpMessage);
};

class FsckContact _kc_final : public Fsck {
private:
	HRESULT ValidateItem(LPMESSAGE lpMessage, const std::string &strClass) _kc_override;
	HRESULT ValidateContactNames(LPMESSAGE lpMessage);
};

class FsckTask _kc_final : public Fsck {
private:
	HRESULT ValidateItem(LPMESSAGE lpMessage, const std::string &strClass) _kc_override;
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
