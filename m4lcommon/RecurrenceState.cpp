/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#include <kopano/zcdefs.h>
#include <kopano/platform.h>
#include <utility>
#include <cstdio>
#include <mapi.h>
#include <mapix.h>
#include <mapicode.h>
#include <kopano/stringutil.h>
#include <kopano/RecurrenceState.h>
#include <kopano/charset/convert.h>

#ifndef WIN32
#define DEBUGREAD 0
#if DEBUGREAD
#include <arpa/inet.h>			// nasty hack to display write bytes as read bytes using hton.()
#define DEBUGPRINT(x, args...) fprintf(stderr, x, ##args)
#else
#define DEBUGPRINT(x, args...)
#endif
#else
// Testing for both WIN32 && LINUX makes no f sense
		#define DEBUGPRINT(...)
#endif

namespace KC {

class BinReader _kc_final {
public:
	BinReader(const char *lpData, unsigned int ulLen) :
		m_lpData(lpData), m_ulLen(ulLen)
	{}
    
    int ReadByte(unsigned int *lpData) {
        if(m_ulCursor + 1 > m_ulLen)
            return -1;
            
		DEBUGPRINT("%s ", bin2hex(1, m_lpData + m_ulCursor).c_str());
        *lpData = m_lpData[m_ulCursor];
        m_ulCursor+=1;
        
        DEBUGPRINT("%10u %08X ", *lpData, *lpData);
        return 1;
    };
    
    int ReadShort(unsigned int *lpData) {
        if(m_ulCursor + 2 > m_ulLen)
            return -1;
            
		DEBUGPRINT("%s ", bin2hex(2, m_lpData + m_ulCursor).c_str());
		unsigned short tmp;
		memcpy(&tmp, m_lpData + m_ulCursor, sizeof(tmp));
		*lpData = le16_to_cpu(tmp);
        m_ulCursor+=2;
        
        DEBUGPRINT("%10u %08X ", *lpData, *lpData);
        return 2;
    };

    int ReadLong(unsigned int *lpData) {
        if(m_ulCursor + 4 > m_ulLen)
            return -1;
            
		DEBUGPRINT("%s ", bin2hex(4, m_lpData + m_ulCursor).c_str());
		unsigned int tmp;
		memcpy(&tmp, m_lpData + m_ulCursor, sizeof(tmp));
		*lpData = le32_to_cpu(tmp);
        m_ulCursor+=4;
        
        DEBUGPRINT("%10u %08X ", *lpData, *lpData);
        return 4;
    };

    int ReadString(std::string *lpData, unsigned int len) {
        unsigned int reallen = len > m_ulLen - m_ulCursor ? m_ulLen - m_ulCursor : len;
        
        if(m_ulCursor + reallen > m_ulLen)
            return -1;

        if(reallen)      
			DEBUGPRINT("%s ", bin2hex(len > m_ulLen - m_ulCursor ? m_ulLen - m_ulCursor : len, m_lpData+m_ulCursor).c_str());
        
        lpData->assign(&m_lpData[m_ulCursor], reallen);
        
        m_ulCursor+=reallen;
        
        if(reallen)
            DEBUGPRINT("\"%s\" ", lpData->c_str());
        return reallen == len ? reallen : -1;
    };
    
	int GetCursorPos(void) const { return m_ulCursor; }
    
private:
	const char *m_lpData;
	unsigned int m_ulLen, m_ulCursor = 0;
};

class BinWriter _kc_final {
public:
    void GetData(char **lppData, unsigned int *lpulLen, void *base) {
        char *lpData;
		auto hr = MAPIAllocateMore(m_strData.size(), base, reinterpret_cast<void **>(&lpData));
	if (hr == hrSuccess)
		memcpy(lpData, m_strData.c_str(), m_strData.size());
        
        *lppData = lpData;
        *lpulLen = m_strData.size();
    }
    
    int WriteByte(unsigned int b) {
		m_strData.append(1, static_cast<char>(b));
        return 1;
    }
    
    int WriteShort(unsigned short s) {
		s = cpu_to_le16(s);
        m_strData.append((char *)&s, 2);
        return 2;
    }
    
    int WriteLong(unsigned int l) {
		l = cpu_to_le32(l);
        m_strData.append((char *)&l, 4);
        return 4;
    }
    
    int WriteString(const char *data, unsigned int len) {
        std::string s(data, len);
        
        m_strData += s;
        return len;
    }
    
private:
    std::string m_strData;
};

#define READDATA(x, type) \
	do { \
		if (data.Read##type(&(x)) < 0) { \
			hr = MAPI_E_NOT_FOUND; \
			goto exit; \
		} \
		DEBUGPRINT("%s\n", #x); \
	} while (false)

#define READSTRING(x, len) \
	do { \
		if (data.ReadString(&(x), len) < 0) { \
			hr = MAPI_E_NOT_FOUND; \
			goto exit; \
		} \
		if (len > 0) \
			DEBUGPRINT("%s\n", #x); \
	} while (false)
                            
#define READSHORT(x) READDATA(x, Short)
#define READLONG(x) READDATA(x, Long)

#define WRITESHORT(x) do { \
		DEBUGPRINT("%04X %10u %08X %s\n", htons(x), (x), (x), #x); \
		data.WriteShort(x); \
	} while (false)
#define WRITELONG(x) do { \
		DEBUGPRINT("%08X %10u %08X %s\n", htonl(x), (x), (x), #x); \
		data.WriteLong(x); \
	} while (false)
#define WRITESTRING(x, l) do { \
		DEBUGPRINT("%d\t%s\t%s\n", (l), (x), #x); \
		data.WriteString((x), (l)); \
	} while (false)

/**
 * Reads exception data from outlook blob.
 *
 * If extended version is not available, it will be synced here. If it is available, it will overwrite the subject and location exceptions (if any) from extended to normal.
 *
 * @param[in]	lpData	blob data
 * @param[in]	ulLen	length of lpData
 * @param[in]	ulFlags	possible task flag
 */
HRESULT RecurrenceState::ParseBlob(const char *lpData, unsigned int ulLen,
    ULONG ulFlags)
{
    HRESULT hr = hrSuccess;
    unsigned int ulReservedBlock1Size, ulReservedBlock2Size;
    bool bReadValid = false; // Read is valid if first set of exceptions was read ok
	bool bExtended = false;	 // false if we need to sync extended data from "normal" data
	convert_context converter;

    BinReader data(lpData, ulLen);

	lstDeletedInstanceDates.clear();
	lstModifiedInstanceDates.clear();
	lstExceptions.clear();
	lstExtendedExceptions.clear();

    READSHORT(ulReaderVersion);     READSHORT(ulWriterVersion);
    READSHORT(ulRecurFrequency);    READSHORT(ulPatternType);
    READSHORT(ulCalendarType);
    READLONG(ulFirstDateTime);
    READLONG(ulPeriod);
    READLONG(ulSlidingFlag);
    
    if (ulPatternType == PT_DAY) {
        // No patterntype specific
    } else if (ulPatternType == PT_WEEK) {
        READLONG(ulWeekDays);
    } else if (ulPatternType == PT_MONTH || ulPatternType == PT_MONTH_END ||
        ulPatternType == PT_HJ_MONTH || ulPatternType == PT_HJ_MONTH_END) {
        READLONG(ulDayOfMonth);
    } else if (ulPatternType == PT_MONTH_NTH ||
        ulPatternType == PT_HJ_MONTH_NTH) {
        READLONG(ulWeekDays);
        READLONG(ulWeekNumber);
    }
    
    READLONG(ulEndType);
    READLONG(ulOccurrenceCount);
    READLONG(ulFirstDOW);
    READLONG(ulDeletedInstanceCount);
	for (unsigned int i = 0; i < ulDeletedInstanceCount; ++i) {
        unsigned int ulDeletedInstanceDate;
        READLONG(ulDeletedInstanceDate);
		lstDeletedInstanceDates.emplace_back(ulDeletedInstanceDate);
    }
    
    READLONG(ulModifiedInstanceCount);
	for (unsigned int i = 0; i < ulModifiedInstanceCount; ++i) {
        unsigned int ulModifiedInstanceDate;
        READLONG(ulModifiedInstanceDate);
		lstModifiedInstanceDates.emplace_back(ulModifiedInstanceDate);
    }
    
    READLONG(ulStartDate);
    READLONG(ulEndDate);

	if (ulFlags & RECURRENCE_STATE_TASKS)
		bReadValid = true; // Task recurrence

    READLONG(ulReaderVersion2);
    READLONG(ulWriterVersion2);
    READLONG(ulStartTimeOffset);
    READLONG(ulEndTimeOffset);

    READSHORT(ulExceptionCount);
	for (unsigned int i = 0; i < ulExceptionCount; ++i) {
		unsigned int ulSubjectLength, ulSubjectLength2;
		unsigned int ulLocationLength, ulLocationLength2;
        Exception sException;
        
        READLONG(sException.ulStartDateTime);
        READLONG(sException.ulEndDateTime);
        READLONG(sException.ulOriginalStartDate);
        READSHORT(sException.ulOverrideFlags);
        
        if(sException.ulOverrideFlags & ARO_SUBJECT) {
            READSHORT(ulSubjectLength);
            READSHORT(ulSubjectLength2);
            READSTRING(sException.strSubject, ulSubjectLength2);
        }
        if (sException.ulOverrideFlags & ARO_MEETINGTYPE)
            READLONG(sException.ulApptStateFlags);
        if (sException.ulOverrideFlags & ARO_REMINDERDELTA)
            READLONG(sException.ulReminderDelta);
        if (sException.ulOverrideFlags & ARO_REMINDERSET)
            READLONG(sException.ulReminderSet);
        if(sException.ulOverrideFlags & ARO_LOCATION) {
            READSHORT(ulLocationLength);
            READSHORT(ulLocationLength2);
            READSTRING(sException.strLocation, ulLocationLength2);
        }
        if (sException.ulOverrideFlags & ARO_BUSYSTATUS)
            READLONG(sException.ulBusyStatus);
        if (sException.ulOverrideFlags & ARO_ATTACHMENT)
            READLONG(sException.ulAttachment);
        if (sException.ulOverrideFlags & ARO_SUBTYPE)
            READLONG(sException.ulSubType);
        if (sException.ulOverrideFlags & ARO_APPTCOLOR)
            READLONG(sException.ulAppointmentColor);
		lstExceptions.emplace_back(std::move(sException));
    }
    
    bReadValid  = true;
    
    READLONG(ulReservedBlock1Size);
    READSTRING(strReservedBlock1, ulReservedBlock1Size);

    for (auto &exc : lstExceptions) {
        ExtendedException sExtendedException;
		unsigned int ulReservedBlock1Size, ulReservedBlock2Size;
		unsigned int ulWideCharSubjectLength, ulWideCharLocationLength;
        unsigned int ulChangeHighlightSize;
        
        if(ulWriterVersion2 >= 0x00003009) {
            READLONG(ulChangeHighlightSize);
            READLONG(sExtendedException.ulChangeHighlightValue);
            READSTRING(sExtendedException.strReserved, ulChangeHighlightSize-4);
        }
        
        READLONG(ulReservedBlock1Size);
        READSTRING(sExtendedException.strReservedBlock1, ulReservedBlock1Size);
        
        // According to the docs, these are condition depending on the OverrideFlags field. But that's wrong.
        if (exc.ulOverrideFlags & ARO_SUBJECT ||
            exc.ulOverrideFlags & ARO_LOCATION) {
            READLONG(sExtendedException.ulStartDateTime);
            READLONG(sExtendedException.ulEndDateTime);
            READLONG(sExtendedException.ulOriginalStartDate);
        }
        
        if (exc.ulOverrideFlags & ARO_SUBJECT) {
			std::string strBytes;
            READSHORT(ulWideCharSubjectLength);
            READSTRING(strBytes, ulWideCharSubjectLength * sizeof(short));
			TryConvert(converter, strBytes, ulWideCharSubjectLength * sizeof(short), "UCS-2LE", sExtendedException.strWideCharSubject);
        }

        if (exc.ulOverrideFlags & ARO_LOCATION) {
			std::string strBytes;
            READSHORT(ulWideCharLocationLength);
            READSTRING(strBytes, ulWideCharLocationLength * sizeof(short));
			TryConvert(converter, strBytes, ulWideCharLocationLength * sizeof(short), "UCS-2LE", sExtendedException.strWideCharLocation);
        }
        
        if (exc.ulOverrideFlags & ARO_SUBJECT ||
            exc.ulOverrideFlags & ARO_LOCATION) {
            READLONG(ulReservedBlock2Size);
            READSTRING(sExtendedException.strReservedBlock2, ulReservedBlock2Size);
        }
		lstExtendedExceptions.emplace_back(std::move(sExtendedException));
    }
	bExtended = true;

    READLONG(ulReservedBlock2Size);
    READSTRING(strReservedBlock2, ulReservedBlock2Size);

    DEBUGPRINT("%d Bytes left\n", ulLen - data.GetCursorPos());
    
	if (ulLen - data.GetCursorPos() != 0)
		hr = MAPI_E_NOT_FOUND;
exit:
	if (hr == hrSuccess || !bReadValid)
		return hr;
        hr = MAPI_W_ERRORS_RETURNED;
	// sync normal exceptions to extended exceptions, it those aren't present
	if (bExtended)
		return hr;

	lstExtendedExceptions.clear(); // remove any half exception maybe read
	for (ULONG i = 0; i < ulExceptionCount; ++i) {
		ExtendedException cEx;
		cEx.ulChangeHighlightValue = 0;
		cEx.ulStartDateTime = lstExceptions[i].ulStartDateTime;
		cEx.ulEndDateTime = lstExceptions[i].ulEndDateTime;
		cEx.ulOriginalStartDate = lstExceptions[i].ulOriginalStartDate;
		// subject & location in UCS2
		if (lstExceptions[i].ulOverrideFlags & ARO_SUBJECT)
			TryConvert(converter, lstExceptions[i].strSubject, rawsize(lstExceptions[i].strSubject), "windows-1252", cEx.strWideCharSubject);
		if (lstExceptions[i].ulOverrideFlags & ARO_LOCATION)
			TryConvert(converter, lstExceptions[i].strLocation, rawsize(lstExceptions[i].strLocation), "windows-1252", cEx.strWideCharLocation);
		lstExtendedExceptions.emplace_back(cEx);

		// clear for next exception
		cEx.strWideCharSubject.clear();
		cEx.strWideCharLocation.clear();
	}
	return hr;
}

/**
 * Write exception data.
 *
 * All exception data, including extended data, must be available in this class.
 *
 * @param[out]	lppData	output blob
 * @param[out]	lpulLen	lenght of lppData
 * @parampin]	base	base pointer for allocation, may be NULL to start new chainn of MAPIAllocateBuffer
 */
HRESULT RecurrenceState::GetBlob(char **lppData, unsigned int *lpulLen, void *base)
{
    BinWriter data;
    std::vector<Exception>::const_iterator j = lstExceptions.begin();
    
    // There is one hard requirement: there must be as many Exceptions as there are ExtendedExceptions. Other
    // inconstencies are also bad, but we need at least that to even write the stream
    
	if (lstExceptions.size() != lstExtendedExceptions.size())
		return MAPI_E_CORRUPT_DATA;
    
    WRITESHORT(ulReaderVersion); 		WRITESHORT(ulWriterVersion);
    WRITESHORT(ulRecurFrequency);		WRITESHORT(ulPatternType);
    WRITESHORT(ulCalendarType);
    WRITELONG(ulFirstDateTime);
    WRITELONG(ulPeriod);
    WRITELONG(ulSlidingFlag);
    
    if (ulPatternType == PT_DAY) {
        // No data
    } else if (ulPatternType == PT_WEEK) {
        WRITELONG(ulWeekDays);
    } else if (ulPatternType == PT_MONTH || ulPatternType == PT_MONTH_END ||
        ulPatternType == PT_HJ_MONTH || ulPatternType == PT_HJ_MONTH_END) {
        WRITELONG(ulDayOfMonth);
    } else if (ulPatternType == PT_MONTH_NTH ||
        ulPatternType == PT_HJ_MONTH_NTH) {
        WRITELONG(ulWeekDays);
        WRITELONG(ulWeekNumber);
    }
    
    WRITELONG(ulEndType);
    WRITELONG(ulOccurrenceCount);
    WRITELONG(ulFirstDOW);
    WRITELONG(ulDeletedInstanceCount);
    
	for (const auto i : lstDeletedInstanceDates)
		WRITELONG(i);
    
    WRITELONG(ulModifiedInstanceCount);
    
	for (const auto i : lstModifiedInstanceDates)
		WRITELONG(i);
    
    WRITELONG(ulStartDate);
    WRITELONG(ulEndDate);
    
    WRITELONG(ulReaderVersion2);
    WRITELONG(ulWriterVersion2);
    WRITELONG(ulStartTimeOffset);
    WRITELONG(ulEndTimeOffset);
    
    WRITESHORT(ulExceptionCount);
    
	for (const auto &i : lstExceptions) {
		WRITELONG(i.ulStartDateTime);
		WRITELONG(i.ulEndDateTime);
		WRITELONG(i.ulOriginalStartDate);
		WRITESHORT(i.ulOverrideFlags);
		if (i.ulOverrideFlags & ARO_SUBJECT) {
			WRITESHORT(static_cast<ULONG>(i.strSubject.size() + 1));
			WRITESHORT(static_cast<ULONG>(i.strSubject.size()));
			WRITESTRING(i.strSubject.c_str(), static_cast<ULONG>(i.strSubject.size()));
		}
		if (i.ulOverrideFlags & ARO_MEETINGTYPE)
			WRITELONG(i.ulApptStateFlags);
		if (i.ulOverrideFlags & ARO_REMINDERDELTA)
			WRITELONG(i.ulReminderDelta);
		if (i.ulOverrideFlags & ARO_REMINDERSET)
			WRITELONG(i.ulReminderSet);
		if (i.ulOverrideFlags & ARO_LOCATION) {
			WRITESHORT(static_cast<ULONG>(i.strLocation.size()) + 1);
			WRITESHORT(static_cast<ULONG>(i.strLocation.size()));
			WRITESTRING(i.strLocation.c_str(), static_cast<ULONG>(i.strLocation.size()));
		}
		if (i.ulOverrideFlags & ARO_BUSYSTATUS)
			WRITELONG(i.ulBusyStatus);
		if (i.ulOverrideFlags & ARO_ATTACHMENT)
			WRITELONG(i.ulAttachment);
		if (i.ulOverrideFlags & ARO_SUBTYPE)
			WRITELONG(i.ulSubType);
		if (i.ulOverrideFlags & ARO_APPTCOLOR)
			WRITELONG(i.ulAppointmentColor);
	}
    
    WRITELONG((ULONG)strReservedBlock1.size());
    WRITESTRING(strReservedBlock1.c_str(), (ULONG)strReservedBlock1.size());

	for (const auto &i : lstExtendedExceptions) {
		if (ulWriterVersion2 >= 0x00003009) {
			WRITELONG(static_cast<ULONG>(i.strReserved.size() + 4));
			WRITELONG(i.ulChangeHighlightValue);
			WRITESTRING(i.strReserved.c_str(), static_cast<ULONG>(i.strReserved.size()));
		}

		WRITELONG(static_cast<ULONG>(i.strReservedBlock1.size()));
		WRITESTRING(i.strReservedBlock1.c_str(), static_cast<ULONG>(i.strReservedBlock1.size()));

		if ((j->ulOverrideFlags & ARO_SUBJECT) || (j->ulOverrideFlags & ARO_LOCATION)) {
			WRITELONG(i.ulStartDateTime);
			WRITELONG(i.ulEndDateTime);
			WRITELONG(i.ulOriginalStartDate);
		}
		if (j->ulOverrideFlags & ARO_SUBJECT) {
			auto strWide = convert_to<std::u16string>(i.strWideCharSubject);
			WRITESHORT(static_cast<ULONG>(strWide.size()));
			WRITESTRING(reinterpret_cast<const char *>(strWide.c_str()), static_cast<ULONG>(strWide.size()) * 2);
		}
		if (j->ulOverrideFlags & ARO_LOCATION) {
			auto strWide = convert_to<std::u16string>(i.strWideCharLocation);
			WRITESHORT(static_cast<ULONG>(strWide.size()));
			WRITESTRING(reinterpret_cast<const char *>(strWide.c_str()), static_cast<ULONG>(strWide.size()) * 2);
		}
		if ((j->ulOverrideFlags & ARO_SUBJECT) || (j->ulOverrideFlags & ARO_LOCATION)) {
			WRITELONG(static_cast<ULONG>(i.strReservedBlock2.size()));
			WRITESTRING(i.strReservedBlock2.c_str(), static_cast<ULONG>(i.strReservedBlock2.size()));
		}
		++j;
	}

    WRITELONG((ULONG)strReservedBlock2.size());
    WRITESTRING(strReservedBlock2.c_str(), (ULONG)strReservedBlock2.size());
    
	data.GetData(lppData, lpulLen, base);
	return hrSuccess;
}

} /* namespace */
