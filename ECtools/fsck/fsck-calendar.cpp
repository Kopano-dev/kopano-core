/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/platform.h>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <kopano/CommonUtil.h>
#include <kopano/mapiext.h>
#include <kopano/mapiguidext.h>
#include <kopano/memory.hpp>
#include <mapiutil.h>
#include <mapix.h>
#include <kopano/namedprops.h>
#include <kopano/charset/convert.h>
#include <kopano/RecurrenceState.h>
#include <kopano/timeutil.hpp>
#include "fsck.h"

using namespace KC;

static time_t operator-(const FILETIME &a, const FILETIME &b)
{
	return FileTimeToUnixTime(a) - FileTimeToUnixTime(b);
}

HRESULT FsckCalendar::ValidateMinimalNamedFields(LPMESSAGE lpMessage)
{

	enum {
		E_REMINDER,
		E_ALLDAYEVENT,
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
	ta[E_ALLDAYEVENT]->lpguid = const_cast<GUID *>(&PSETID_Appointment);
	ta[E_ALLDAYEVENT]->ulKind = MNID_ID;
	ta[E_ALLDAYEVENT]->Kind.lID = dispidAllDayEvent;

	strTagName[E_REMINDER] = "dispidReminderSet";
	strTagName[E_ALLDAYEVENT] = "dispidAllDayEvent";

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

HRESULT FsckCalendar::ValidateTimestamps(LPMESSAGE lpMessage)
{
	memory_ptr<SPropValue> lpPropertyArray;
	memory_ptr<SPropTagArray> lpPropertyTagArray;
	memory_ptr<MAPINAMEID *> ta;
	const FILETIME *lpStart, *lpEnd, *lpCommonStart, *lpCommonEnd;

	enum {
		E_START,
		E_END,
		E_CSTART,
		E_CEND,
		E_DURATION,
		TAG_COUNT
	};
	/*
	 * Allocate the NamedID list and initialize it to all
	 * properties which could give us some information about the name.
	 */
	auto hr = allocNamedIdList(TAG_COUNT, &~ta);
	if (hr != hrSuccess)
		return hr;

	ta[E_START]->lpguid = const_cast<GUID *>(&PSETID_Appointment);
	ta[E_START]->ulKind = MNID_ID;
	ta[E_START]->Kind.lID = dispidApptStartWhole;
	ta[E_END]->lpguid = const_cast<GUID *>(&PSETID_Appointment);
	ta[E_END]->ulKind = MNID_ID;
	ta[E_END]->Kind.lID = dispidApptEndWhole;
	ta[E_CSTART]->lpguid = const_cast<GUID *>(&PSETID_Common);
	ta[E_CSTART]->ulKind = MNID_ID;
	ta[E_CSTART]->Kind.lID = dispidCommonStart;
	ta[E_CEND]->lpguid = const_cast<GUID *>(&PSETID_Common);
	ta[E_CEND]->ulKind = MNID_ID;
	ta[E_CEND]->Kind.lID = dispidCommonEnd;
	ta[E_DURATION]->lpguid = const_cast<GUID *>(&PSETID_Appointment);
	ta[E_DURATION]->ulKind = MNID_ID;
	ta[E_DURATION]->Kind.lID = dispidApptDuration;

	hr = ReadNamedProperties(lpMessage, TAG_COUNT, ta,
	     &~lpPropertyTagArray, &~lpPropertyArray);
	if (FAILED(hr))
		return hr;
	/*
	 * Validate parameters:
	 * If E_START is missing it can be substituted with E_CSTART and vice versa
	 * If E_END is missing it can be substituted with E_CEND and vice versa
	 * E_{C}START < E_{C}END
	 * E_{C}END - E_{C}START / 60 = Duration
	 * If duration is missing, calculate and set it.
	 */
	if (PROP_TYPE(lpPropertyArray[E_START].ulPropTag) == PT_ERROR) {
		if (PROP_TYPE(lpPropertyArray[E_CSTART].ulPropTag) == PT_ERROR) {
			std::cout << "No valid starting address could be detected." << std::endl;
			return E_INVALIDARG;
		}
		hr = AddMissingProperty(lpMessage, "dispidApptStartWhole",
					CHANGE_PROP_TYPE(lpPropertyTagArray->aulPropTag[E_START], PT_SYSTIME),
					lpPropertyArray[E_CSTART].Value);
		if (hr != hrSuccess)
			return hr;
		lpStart = &lpPropertyArray[E_CSTART].Value.ft;
	} else
		lpStart = &lpPropertyArray[E_START].Value.ft;

	if (PROP_TYPE(lpPropertyArray[E_CSTART].ulPropTag) == PT_ERROR) {
		if (PROP_TYPE(lpPropertyArray[E_START].ulPropTag) == PT_ERROR) {
			std::cout << "No valid starting address could be detected." << std::endl;
			return E_INVALIDARG;
		}
		hr = AddMissingProperty(lpMessage, "dispidCommonStart",
					CHANGE_PROP_TYPE(lpPropertyTagArray->aulPropTag[E_CSTART], PT_SYSTIME),
					lpPropertyArray[E_START].Value);
		if (hr != hrSuccess)
			return hr;
		lpCommonStart = &lpPropertyArray[E_START].Value.ft;
	} else
		lpCommonStart = &lpPropertyArray[E_CSTART].Value.ft;

	if (PROP_TYPE(lpPropertyArray[E_END].ulPropTag) == PT_ERROR) {
		if (PROP_TYPE(lpPropertyArray[E_CEND].ulPropTag) == PT_ERROR) {
			std::cout << "No valid end address could be detected." << std::endl;
			return E_INVALIDARG;
		}
		hr = AddMissingProperty(lpMessage, "dispidApptEndWhole",
					CHANGE_PROP_TYPE(lpPropertyTagArray->aulPropTag[E_END], PT_SYSTIME),
					lpPropertyArray[E_CEND].Value);
		if (hr != hrSuccess)
			return hr;
		lpEnd = &lpPropertyArray[E_CEND].Value.ft;
	} else
		lpEnd = &lpPropertyArray[E_END].Value.ft;

	if (PROP_TYPE(lpPropertyArray[E_CEND].ulPropTag) == PT_ERROR) {
		if (PROP_TYPE(lpPropertyArray[E_END].ulPropTag) == PT_ERROR) {
			std::cout << "No valid starting address could be detected." << std::endl;
			return E_INVALIDARG;
		}
		hr = AddMissingProperty(lpMessage, "dispidCommonEnd",
					CHANGE_PROP_TYPE(lpPropertyTagArray->aulPropTag[E_CEND], PT_SYSTIME),
					lpPropertyArray[E_END].Value);
		if (hr != hrSuccess)
			return hr;
		lpCommonEnd = &lpPropertyArray[E_END].Value.ft;
	} else
		lpCommonEnd = &lpPropertyArray[E_CEND].Value.ft;

	if (*lpStart > *lpEnd && *lpCommonStart < *lpCommonEnd) {
		hr = ReplaceProperty(lpMessage, "dispidApptStartWhole",
				     CHANGE_PROP_TYPE(lpPropertyTagArray->aulPropTag[E_START], PT_SYSTIME),
				     "Whole start after whole end date.",
				     lpPropertyArray[E_CSTART].Value);
		if (hr != hrSuccess)
			return hr;
		hr = ReplaceProperty(lpMessage, "dispidApptEndWhole",
				     CHANGE_PROP_TYPE(lpPropertyTagArray->aulPropTag[E_END], PT_SYSTIME),
				     "Whole start after whole end date.",
				     lpPropertyArray[E_CEND].Value);
		if (hr != hrSuccess)
			return hr;
	}

	if (*lpCommonStart > *lpCommonEnd && *lpStart < *lpEnd) {
		hr = ReplaceProperty(lpMessage, "dispidCommonStart",
				     CHANGE_PROP_TYPE(lpPropertyTagArray->aulPropTag[E_CSTART], PT_SYSTIME),
				     "Common start after common end date.",
				     lpPropertyArray[E_START].Value);
		if (hr != hrSuccess)
			return hr;
		hr = ReplaceProperty(lpMessage, "dispidCommonEnd",
				     CHANGE_PROP_TYPE(lpPropertyTagArray->aulPropTag[E_CEND], PT_SYSTIME),
				     "Common start after common end date.",
				     lpPropertyArray[E_END].Value);
		if (hr != hrSuccess)
			return hr;
	}

	if ((*lpEnd - *lpStart) != (*lpCommonEnd - *lpCommonStart)) {
		std::cout << "Difference in duration: " << std::endl;
		std::cout << "Common duration (" << (*lpCommonEnd - *lpCommonStart) << ") ";
		std::cout << "- Whole duration (" << (*lpEnd - *lpStart) << ")" <<std::endl;
		return E_INVALIDARG;
	}

	/*
	 * Common duration matches whole duration,
	 * now we need to compare the duration itself.
	 */
	__UPV Value;
	Value.l = (*lpEnd - *lpStart) / 60;

	if (PROP_TYPE(lpPropertyArray[E_DURATION].ulPropTag) == PT_ERROR)
		return AddMissingProperty(lpMessage, "dispidApptDuration",
					CHANGE_PROP_TYPE(lpPropertyTagArray->aulPropTag[E_DURATION], PT_LONG),
					Value);

	auto ulDuration = lpPropertyArray[E_DURATION].Value.l;
	/*
	 * We already compared duration between common and start,
	 * now we have to check if that duration also equals what was set.
	 */
	if (ulDuration != Value.l)
		return ReplaceProperty(lpMessage, "dispidApptDuration",
		       CHANGE_PROP_TYPE(lpPropertyTagArray->aulPropTag[E_DURATION], PT_LONG),
		       "Duration does not match (End - Start / 60)",
		       Value);
	return hrSuccess;
}

HRESULT FsckCalendar::ValidateRecurrence(LPMESSAGE lpMessage)
{
	memory_ptr<SPropValue> lpPropertyArray;
	memory_ptr<SPropTagArray> lpPropertyTagArray;
	bool bRecurring = false;
	LONG ulType = 0;
	std::string lpData;
	memory_ptr<MAPINAMEID *> ta;
	size_t ulLen = 0;

	enum {
		E_RECURRENCE,
		E_RECURRENCE_TYPE,
		E_RECURRENCE_PATTERN,
		E_RECURRENCE_STATE,
		TAG_COUNT
	};
	/*
	 * Allocate the NamedID list and initialize it to all
	 * properties which could give us some information about the name.
	 */
	auto hr = allocNamedIdList(TAG_COUNT, &~ta);
	if (hr != hrSuccess)
		return hr;

	ta[E_RECURRENCE]->lpguid = const_cast<GUID *>(&PSETID_Appointment);
	ta[E_RECURRENCE]->ulKind = MNID_ID;
	ta[E_RECURRENCE]->Kind.lID = dispidRecurring;
	ta[E_RECURRENCE_TYPE]->lpguid = const_cast<GUID *>(&PSETID_Appointment);
	ta[E_RECURRENCE_TYPE]->ulKind = MNID_ID;
	ta[E_RECURRENCE_TYPE]->Kind.lID = dispidRecurrenceType;
	ta[E_RECURRENCE_PATTERN]->lpguid = const_cast<GUID *>(&PSETID_Appointment);
	ta[E_RECURRENCE_PATTERN]->ulKind = MNID_ID;
	ta[E_RECURRENCE_PATTERN]->Kind.lID = dispidRecurrencePattern;
	ta[E_RECURRENCE_STATE]->lpguid = const_cast<GUID *>(&PSETID_Appointment);
	ta[E_RECURRENCE_STATE]->ulKind = MNID_ID;
	ta[E_RECURRENCE_STATE]->Kind.lID = dispidRecurrenceState;

	hr = ReadNamedProperties(lpMessage, TAG_COUNT, ta,
	     &~lpPropertyTagArray, &~lpPropertyArray);
	if (FAILED(hr))
		return hr;

	if (PROP_TYPE(lpPropertyArray[E_RECURRENCE].ulPropTag) == PT_ERROR) {
		__UPV Value;
		/*
		 * Check if the recurrence type is set, and if this is the case,
		 * if the type indicates recurrence.
		 */
		if (PROP_TYPE(lpPropertyArray[E_RECURRENCE_TYPE].ulPropTag) == PT_ERROR ||
		    lpPropertyArray[E_RECURRENCE_TYPE].Value.l == 0)
			Value.b = false;
		else
			Value.b = true;

		hr = AddMissingProperty(lpMessage, "dispidRecurring",
					CHANGE_PROP_TYPE(lpPropertyTagArray->aulPropTag[E_RECURRENCE], PT_BOOLEAN),
					Value);
		if (hr != hrSuccess)
			return hr;
		bRecurring = Value.b;
	} else
		bRecurring = lpPropertyArray[E_RECURRENCE].Value.b;

	/*
	 * The type has 4 possible values:
	 * 	0 - No recurrence
	 *	1 - Daily
	 *	2 - Weekly
	 *	3 - Monthly
	 *	4 - Yearly
	 */
	if (PROP_TYPE(lpPropertyArray[E_RECURRENCE_TYPE].ulPropTag) != PT_ERROR) {
		ulType = lpPropertyArray[E_RECURRENCE_TYPE].Value.l;
	} else if (bRecurring) {
		std::cout << "Item is recurring but is missing recurrence type" << std::endl;
		return E_INVALIDARG;
	} else {
		ulType = 0;
	}

	if (!bRecurring && ulType > 0) {
		__UPV Value;
		Value.l = 0;
		hr = ReplaceProperty(lpMessage, "dispidRecurrenceType",
				     CHANGE_PROP_TYPE(lpPropertyTagArray->aulPropTag[E_RECURRENCE_TYPE], PT_LONG),
				     "No recurrence, but recurrence type is > 0.",
				     Value);
		if (hr != hrSuccess)
			return hr;
	} else if (bRecurring && ulType == 0) {
		__UPV Value;
		Value.b = false;
		hr = ReplaceProperty(lpMessage, "dispidRecurring",
				     CHANGE_PROP_TYPE(lpPropertyTagArray->aulPropTag[E_RECURRENCE], PT_BOOLEAN),
				     "Recurrence has been set, but type indicates no recurrence.",
				     Value);
		if (hr != hrSuccess)
			return hr;
	} else if (ulType > 4) {
		__UPV Value;

		if (bRecurring) {
			Value.b = false;
			bRecurring = false;
			hr = ReplaceProperty(lpMessage, "dispidRecurring",
					     CHANGE_PROP_TYPE(lpPropertyTagArray->aulPropTag[E_RECURRENCE], PT_BOOLEAN),
					     "Invalid recurrence type, disabling recurrence.",
					     Value);
			if (hr != hrSuccess)
				return hr;
		}

		Value.l = 0;
		ulType = 0;
		hr = ReplaceProperty(lpMessage, "dispidRecurrenceType",
				     CHANGE_PROP_TYPE(lpPropertyTagArray->aulPropTag[E_RECURRENCE], PT_LONG),
				     "Invalid recurrence type, disabling recurrence.",
				     Value);
		if (hr != hrSuccess)
			return hr;
	}

	if ((PROP_TYPE(lpPropertyArray[E_RECURRENCE_PATTERN].ulPropTag) == PT_ERROR ||
	    strcmp(lpPropertyArray[E_RECURRENCE_PATTERN].Value.lpszA, "") == 0) && bRecurring) {
		__UPV Value;

		switch (ulType) {
		case 1:
			Value.lpszA = const_cast<char *>("Daily");
			break;
		case 2:
			Value.lpszA = const_cast<char *>("Weekly");
			break;
		case 3:
			Value.lpszA = const_cast<char *>("Monthly");
			break;
		case 4:
			Value.lpszA = const_cast<char *>("Yearly");
			break;
		default:
			Value.lpszA = const_cast<char *>("Invalid");
			break;
		}

		hr = AddMissingProperty(lpMessage, "dispidRecurrencePattern",
					CHANGE_PROP_TYPE(lpPropertyTagArray->aulPropTag[E_RECURRENCE_PATTERN], PT_STRING8),
					Value);
		if (hr != hrSuccess)
			return hr;
	}

	if (!bRecurring || PROP_TYPE(lpPropertyArray[E_RECURRENCE_STATE].ulPropTag) == PT_ERROR)
		return hrSuccess;

	// Check the actual recurrence state
	RecurrenceState r;
	__UPV Value;

	hr = r.ParseBlob(reinterpret_cast<char *>(lpPropertyArray[E_RECURRENCE_STATE].Value.bin.lpb), lpPropertyArray[E_RECURRENCE_STATE].Value.bin.cb, RECURRENCE_STATE_CALENDAR);
	if (hr != hrSuccess && hr != MAPI_W_ERRORS_RETURNED) {
		/* Recurrence state is useless */
		Value.l = 0;
		return ReplaceProperty(lpMessage, "dispidRecurrenceState",
		       CHANGE_PROP_TYPE(lpPropertyTagArray->aulPropTag[E_RECURRENCE], PT_LONG),
		       "Invalid recurrence state, disabling recurrence.", Value);
	}

	// Recurrence state is readable, but may have errors.
	// First, make sure the number of extended exceptions is correct
	while (r.lstExtendedExceptions.size() > r.lstExceptions.size())
		r.lstExtendedExceptions.erase(--r.lstExtendedExceptions.end());

	// Add new extendedexceptions if missing
	auto iEx = r.lstExceptions.begin();
	std::advance(iEx, r.lstExtendedExceptions.size());

	while (r.lstExtendedExceptions.size() < r.lstExceptions.size()) {
		std::wstring wstr;
		RecurrenceState::ExtendedException ex;

		ex.ulStartDateTime = iEx->ulStartDateTime;
		ex.ulEndDateTime = iEx->ulEndDateTime;
		ex.ulOriginalStartDate = iEx->ulOriginalStartDate;
		TryConvert(iEx->strSubject, rawsize(iEx->strSubject), "windows-1252", wstr);
		ex.strWideCharSubject = wstr;
		TryConvert(iEx->strLocation, rawsize(iEx->strLocation), "windows-1252", wstr);
		ex.strWideCharLocation = wstr;
		r.lstExtendedExceptions.emplace_back(std::move(ex));
		++iEx;
	}

	// Set some defaults right for exceptions
	for (auto &ex : r.lstExceptions)
		ex.ulOriginalStartDate = (ex.ulOriginalStartDate / 1440) * 1440;

	// Set some defaults for extended exceptions
	iEx = r.lstExceptions.begin();
	for (auto &eex : r.lstExtendedExceptions) {
		std::wstring wstr;
		eex.strReservedBlock1.clear();
		eex.strReservedBlock2.clear();
		eex.ulChangeHighlightValue = 0;
		eex.ulOriginalStartDate = (iEx->ulOriginalStartDate / 1440) * 1440;
		TryConvert(iEx->strSubject, rawsize(iEx->strSubject), "windows-1252", wstr);
		eex.strWideCharSubject = wstr;
		TryConvert(iEx->strLocation, rawsize(iEx->strLocation), "windows-1252", wstr);
		eex.strWideCharLocation = wstr;
		++iEx;
	}

	// Reset reserved data to 0
	r.strReservedBlock1.clear();
	r.strReservedBlock2.clear();

	// These are constant
	r.ulReaderVersion = 0x3004;
	r.ulWriterVersion = 0x3004;

	r.GetBlob(lpData);
	Value.bin.lpb = reinterpret_cast<BYTE *>(const_cast<char *>(lpData.data()));
	Value.bin.cb = lpData.size();

	// Update the recurrence if there is a change
	if (ulLen != lpPropertyArray[E_RECURRENCE_STATE].Value.bin.cb ||
	    memcmp(lpPropertyArray[E_RECURRENCE_STATE].Value.bin.lpb, lpData.c_str(), lpData.size()) != 0)
		hr = ReplaceProperty(lpMessage, "dispidRecurrenceState", CHANGE_PROP_TYPE(lpPropertyArray[E_RECURRENCE_STATE].ulPropTag, PT_BINARY), "Recoverable recurrence state.", Value);
	return hr;
}

HRESULT FsckCalendar::ValidateItem(LPMESSAGE lpMessage,
    const std::string &strClass)
{
	bool bChanged = false;

	if (strClass != "IPM.Appointment") {
		std::cout << "Illegal class: \"" << strClass << "\"" << std::endl;
		return E_INVALIDARG;
	}

	auto hr = ValidateMinimalNamedFields(lpMessage);
	if (hr != hrSuccess)
		return hr;
	hr = ValidateTimestamps(lpMessage);
	if (hr != hrSuccess)
		return hr;
	hr = ValidateRecurrence(lpMessage);
	if (hr != hrSuccess)
		return hr;
	return ValidateRecursiveDuplicateRecipients(lpMessage, bChanged);
}
