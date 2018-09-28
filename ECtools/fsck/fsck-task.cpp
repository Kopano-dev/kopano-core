/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <iostream>
#include <kopano/CommonUtil.h>
#include <kopano/mapiext.h>
#include <kopano/mapiguidext.h>
#include <kopano/memory.hpp>
#include <mapiutil.h>
#include <mapix.h>
#include <kopano/namedprops.h>
#include "fsck.h"

using namespace KC;

HRESULT FsckTask::ValidateMinimalNamedFields(LPMESSAGE lpMessage)
{
	enum {
		E_REMINDER,
		TAG_COUNT
	};

	memory_ptr<SPropValue> lpPropertyArray;
	memory_ptr<SPropTagArray> lpPropertyTagArray;
	memory_ptr<MAPINAMEID *> ta;
	std::string strTagName[TAG_COUNT];

	/*
	 * Allocate the NamedID list and initialize it to all
	 * properties which could give us some information about the name.
	 */
	auto hr = allocNamedIdList(TAG_COUNT, &~ta);
	if (hr != hrSuccess)
		return hr;

	ta[E_REMINDER]->lpguid = const_cast<GUID *>(&PSETID_Common);
	ta[E_REMINDER]->ulKind = MNID_ID;
	ta[E_REMINDER]->Kind.lID = dispidReminderSet;
	strTagName[E_REMINDER] = "dispidReminderSet";

	hr = ReadNamedProperties(lpMessage, TAG_COUNT, ta,
	     &~lpPropertyTagArray, &~lpPropertyArray);
	if (FAILED(hr))
		return hr;

	for (ULONG i = 0; i < TAG_COUNT; ++i) {
		if (PROP_TYPE(lpPropertyArray[i].ulPropTag) != PT_ERROR)
			continue;
		__UPV Value;
		Value.b = false;
		hr = AddMissingProperty(lpMessage, strTagName[i],
					CHANGE_PROP_TYPE(lpPropertyTagArray->aulPropTag[i], PT_BOOLEAN),
					Value);
		if (hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

HRESULT FsckTask::ValidateTimestamps(LPMESSAGE lpMessage)
{
	memory_ptr<SPropValue> lpPropertyArray;
	memory_ptr<SPropTagArray> lpPropertyTagArray;
	memory_ptr<MAPINAMEID *> ta;

	enum {
		E_START_DATE,
		E_DUE_DATE,
		TAG_COUNT
	};

	/*
	 * Allocate the NameID list and initialize it to all
	 * properties which could give us some information about the timestamps.
	 */
	auto hr = allocNamedIdList(TAG_COUNT, &~ta);
	if (hr != hrSuccess)
		return hr;

	ta[E_START_DATE]->lpguid = const_cast<GUID *>(&PSETID_Task);
	ta[E_START_DATE]->ulKind = MNID_ID;
	ta[E_START_DATE]->Kind.lID = dispidTaskStartDate;
	ta[E_DUE_DATE]->lpguid = const_cast<GUID *>(&PSETID_Task);
	ta[E_DUE_DATE]->ulKind = MNID_ID;
	ta[E_DUE_DATE]->Kind.lID = dispidTaskDueDate;

	hr = ReadNamedProperties(lpMessage, TAG_COUNT, ta,
	     &~lpPropertyTagArray, &~lpPropertyArray);
	if (FAILED(hr))
		return hr;

	/*
	 * Validate parameters
	 * When Completion is set, the completion date should have been set.
	 * No further restrictions apply, but we will fill in missing tags
	 * based on the results of the other tags.
	 */
	if (PROP_TYPE(lpPropertyArray[E_START_DATE].ulPropTag) == PT_ERROR ||
	    PROP_TYPE(lpPropertyArray[E_DUE_DATE].ulPropTag) == PT_ERROR)
		return hrSuccess;

	const FILETIME *lpStart = &lpPropertyArray[E_START_DATE].Value.ft;
	const FILETIME *lpDue = &lpPropertyArray[E_DUE_DATE].Value.ft;
	/*
	 * We cannot start a task _after_ it is due.
	 */
	if (!(*lpStart > *lpDue))
		return hr;
	__UPV Value;
	Value.ft = *lpDue;
	return ReplaceProperty(lpMessage, "dispidTaskStartDate",
	       CHANGE_PROP_TYPE(lpPropertyTagArray->aulPropTag[E_START_DATE], PT_SYSTIME),
	       "Start date cannot be after due date",
	       Value);
}

HRESULT FsckTask::ValidateCompletion(LPMESSAGE lpMessage)
{
	memory_ptr<SPropValue> lpPropertyArray;
	memory_ptr<SPropTagArray> lpPropertyTagArray;
	memory_ptr<MAPINAMEID *> ta;
	bool bCompleted;

	enum {
		E_COMPLETE,
		E_PERCENT_COMPLETE,
		E_COMPLETION_DATE,
		TAG_COUNT
	};

	/*
	 * Allocate the NamedID list and initialize it to all
	 * properties which could give us some information about the completion.
	 */
	auto hr = allocNamedIdList(TAG_COUNT, &~ta);
	if (hr != hrSuccess)
		return hr;

	ta[E_COMPLETE]->lpguid = const_cast<GUID *>(&PSETID_Task);
	ta[E_COMPLETE]->ulKind = MNID_ID;
	ta[E_COMPLETE]->Kind.lID = dispidTaskComplete;
	ta[E_PERCENT_COMPLETE]->lpguid = const_cast<GUID *>(&PSETID_Task);
	ta[E_PERCENT_COMPLETE]->ulKind = MNID_ID;
	ta[E_PERCENT_COMPLETE]->Kind.lID = dispidTaskPercentComplete;
	ta[E_COMPLETION_DATE]->lpguid = const_cast<GUID *>(&PSETID_Task);
	ta[E_COMPLETION_DATE]->ulKind = MNID_ID;
	ta[E_COMPLETION_DATE]->Kind.lID = dispidTaskDateCompleted;

	hr = ReadNamedProperties(lpMessage, TAG_COUNT, ta,
	     &~lpPropertyTagArray, &~lpPropertyArray);
	if (FAILED(hr))
		return hr;

	/*
	 * Validate parameters
	 * When Completion is set, the completion date should have been set.
	 * No further restrictions apply, but we will fill in missing tags
	 * based on the results of the other tags.
	 */
	if (PROP_TYPE(lpPropertyArray[E_COMPLETE].ulPropTag) == PT_ERROR) {
		__UPV Value;

		if (((PROP_TYPE(lpPropertyArray[E_PERCENT_COMPLETE].ulPropTag) != PT_ERROR) &&
		     (lpPropertyArray[E_PERCENT_COMPLETE].Value.dbl == 1)) ||
		    (PROP_TYPE((lpPropertyArray[E_COMPLETION_DATE].ulPropTag) != PT_ERROR)))
			Value.b = true;
		else
			Value.b = false;

		hr = AddMissingProperty(lpMessage, "dispidTaskComplete",
					CHANGE_PROP_TYPE(lpPropertyTagArray->aulPropTag[E_COMPLETE], PT_BOOLEAN),
					Value);
		if (hr != hrSuccess)
			return hr;
		bCompleted = Value.b;
	} else
		bCompleted = lpPropertyArray[E_COMPLETE].Value.b;

	if (PROP_TYPE(lpPropertyArray[E_PERCENT_COMPLETE].ulPropTag) == PT_ERROR) {
		__UPV Value;
		Value.dbl = !!bCompleted; /* Value.dbl = 1 => 100% */

		 hr = AddMissingProperty(lpMessage, "dispidTaskPercentComplete",
		 			 CHANGE_PROP_TYPE(lpPropertyTagArray->aulPropTag[E_PERCENT_COMPLETE], PT_DOUBLE),
					 Value);
		if (hr != hrSuccess)
			return hr;
	}

	if (PROP_TYPE(lpPropertyArray[E_COMPLETION_DATE].ulPropTag) == PT_ERROR && bCompleted) {
		__UPV Value;

		GetSystemTimeAsFileTime(&Value.ft);

		hr = AddMissingProperty(lpMessage, "dispidTaskDateCompleted",
					CHANGE_PROP_TYPE(lpPropertyTagArray->aulPropTag[E_COMPLETION_DATE], PT_SYSTIME),
					Value);
		if (hr != hrSuccess)
			return hr;
	}
	return hrSuccess;
}

HRESULT FsckTask::ValidateItem(LPMESSAGE lpMessage,
    const std::string &strClass)
{
	if (strClass != "IPM.Task") {
		std::cout << "Illegal class: \"" << strClass << "\"" << std::endl;
		return E_INVALIDARG;
	}
	auto hr = ValidateMinimalNamedFields(lpMessage);
	if (hr != hrSuccess)
		return hr;
	hr = ValidateTimestamps(lpMessage);
	if (hr != hrSuccess)
		return hr;
	return ValidateCompletion(lpMessage);
}
