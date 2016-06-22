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

#include <kopano/zcdefs.h>
#include <kopano/platform.h>

#include <cstdio>
#include <mapi.h>
#include <mapix.h>
#include <mapicode.h>

#include <kopano/stringutil.h>
#include <kopano/RecurrenceState.h>
#include <kopano/charset/convert.h>
#include <kopano/charset/utf16string.h>

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

class BinReader _zcp_final {
public:
    BinReader(char *lpData, unsigned int ulLen) {
        this->m_lpData = lpData;
        this->m_ulLen = ulLen;
        this->m_ulCursor = 0;
    };
    
    int ReadByte(unsigned int *lpData) {
        if(m_ulCursor + 1 > m_ulLen)
            return -1;
            
        DEBUGPRINT("%s ", bin2hex(1, (BYTE *)m_lpData+m_ulCursor).c_str());
        *lpData = m_lpData[m_ulCursor];
        m_ulCursor+=1;
        
        DEBUGPRINT("%10u %08X ", *lpData, *lpData);
        return 1;
    };
    
    int ReadShort(unsigned int *lpData) {
        if(m_ulCursor + 2 > m_ulLen)
            return -1;
            
        DEBUGPRINT("%s ", bin2hex(2, (BYTE *)m_lpData+m_ulCursor).c_str());
        *lpData = *(unsigned short *)&m_lpData[m_ulCursor];
        m_ulCursor+=2;
        
        DEBUGPRINT("%10u %08X ", *lpData, *lpData);
        return 2;
    };

    int ReadLong(unsigned int *lpData) {
        if(m_ulCursor + 4 > m_ulLen)
            return -1;
            
        DEBUGPRINT("%s ", bin2hex(4, (BYTE *)m_lpData+m_ulCursor).c_str());
        *lpData = *(unsigned int *)&m_lpData[m_ulCursor];
        m_ulCursor+=4;
        
        DEBUGPRINT("%10u %08X ", *lpData, *lpData);
        return 4;
    };

    int ReadString(std::string *lpData, unsigned int len) {
        unsigned int reallen = len > m_ulLen - m_ulCursor ? m_ulLen - m_ulCursor : len;
        
        if(m_ulCursor + reallen > m_ulLen)
            return -1;

        if(reallen)      
            DEBUGPRINT("%s ", bin2hex(len > m_ulLen - m_ulCursor ? m_ulLen - m_ulCursor : len, (BYTE *)m_lpData+m_ulCursor).c_str());
        
        lpData->assign(&m_lpData[m_ulCursor], reallen);
        lpData->substr(0, reallen);
        
        m_ulCursor+=reallen;
        
        if(reallen)
            DEBUGPRINT("\"%s\" ", lpData->c_str());
        return reallen == len ? reallen : -1;
    };
    
    int GetCursorPos() { return m_ulCursor; }
    
private:
    char *m_lpData;
    unsigned int m_ulLen;
    unsigned int m_ulCursor;
};

class BinWriter _zcp_final {
public:
    BinWriter() {};
    ~BinWriter() {};
    
    void GetData(char **lppData, unsigned int *lpulLen, void *base) {
        char *lpData;

	HRESULT hr = hrSuccess;

		if (base)
			hr = MAPIAllocateMore(m_strData.size(), base, (void **)&lpData);
		else
			hr = MAPIAllocateBuffer(m_strData.size(), (void **)&lpData);

	if (hr == hrSuccess)
		memcpy(lpData, m_strData.c_str(), m_strData.size());
        
        *lppData = lpData;
        *lpulLen = m_strData.size();
    }
    
    int WriteByte(unsigned int b) {
        m_strData.append((char *)&b, 1);
        return 1;
    }
    
    int WriteShort(unsigned short s) {
        m_strData.append((char *)&s, 2);
        return 2;
    }
    
    int WriteLong(unsigned int l) {
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

RecurrenceState::RecurrenceState()
{
	ulReaderVersion = 0x3004;
	ulWriterVersion = 0x3004;

	ulRecurFrequency = 0x0000;	// invalid value
	ulPatternType = 0x0000;
	ulCalendarType = 0x0000;
	ulFirstDateTime = 0x0000;
	ulPeriod = 0x0000;
	ulSlidingFlag = 0x0000;

	ulWeekDays = 0x0000;
	ulDayOfMonth = 0x0000;
	ulWeekNumber = 0x0000;

	ulEndType = 0x0000;
	ulOccurrenceCount = 0x0000;
	ulFirstDOW = 0x0001;		// default outlook, monday
	ulDeletedInstanceCount = 0;
	ulModifiedInstanceCount = 0;

	ulStartDate = 0x0000;
	ulEndType = 0x0000;

	ulReaderVersion2 = 0x3006;
	ulWriterVersion2 = 0x3008;	// can also be 3009, but outlook (2003) sets 3008
	ulStartTimeOffset = 0x0000;	// max 1440-1
	ulEndTimeOffset = 0x0000;	// max 1440-1

	ulExceptionCount = 0;
}

RecurrenceState::~RecurrenceState() 
{
}

/**
 * Reads exception data from outlook blob.
 *
 * If extended version is not available, it will be synced here. If it is available, it will overwrite the subject and location exceptions (if any) from extended to normal.
 *
 * @param[in]	lpData	blob data
 * @param[in]	ulLen	length of lpData
 * @param[in]	ulFlags	possible task flag
 */
HRESULT RecurrenceState::ParseBlob(char *lpData, unsigned int ulLen, ULONG ulFlags)
{
    HRESULT hr = hrSuccess;
    unsigned int ulReservedBlock1Size;
    unsigned int ulReservedBlock2Size;
    std::vector<Exception>::const_iterator iterExceptions;
    bool bReadValid = false; // Read is valid if first set of exceptions was read ok
	bool bExtended = false;	 // false if we need to sync extended data from "normal" data
	convert_context converter;

    BinReader data(lpData, ulLen);
    unsigned int i;

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
    
    if(ulPatternType == 0x0000) {
        // No patterntype specific
    } else if(ulPatternType == 0x0001) {
        READLONG(ulWeekDays);
    } else if(ulPatternType == 0x0002 || ulPatternType == 0x0004 || ulPatternType == 0x000a || ulPatternType == 0x000c) {
        READLONG(ulDayOfMonth);
    } else if(ulPatternType == 0x0003 || ulPatternType == 0x000b) {
        READLONG(ulWeekDays);
        READLONG(ulWeekNumber);
    }
    
    READLONG(ulEndType);
    READLONG(ulOccurrenceCount);
    READLONG(ulFirstDOW);
    READLONG(ulDeletedInstanceCount);

    for (i = 0; i < ulDeletedInstanceCount; ++i) {
        unsigned int ulDeletedInstanceDate;
        READLONG(ulDeletedInstanceDate);
        lstDeletedInstanceDates.push_back(ulDeletedInstanceDate);
    }
    
    READLONG(ulModifiedInstanceCount);
    
    for (i = 0; i < ulModifiedInstanceCount; ++i) {
        unsigned int ulModifiedInstanceDate;
        READLONG(ulModifiedInstanceDate);
        lstModifiedInstanceDates.push_back(ulModifiedInstanceDate);
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
    
    for (i = 0; i < ulExceptionCount; ++i) {
        unsigned int ulSubjectLength;
        unsigned int ulSubjectLength2;
        unsigned int ulLocationLength;
        unsigned int ulLocationLength2;
        
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
        
        if(sException.ulOverrideFlags & ARO_MEETINGTYPE) {
            READLONG(sException.ulApptStateFlags);
        }
        
        if(sException.ulOverrideFlags & ARO_REMINDERDELTA) {
            READLONG(sException.ulReminderDelta);
        }
        
        if(sException.ulOverrideFlags & ARO_REMINDERSET) {
            READLONG(sException.ulReminderSet);
        }
             
        if(sException.ulOverrideFlags & ARO_LOCATION) {
            READSHORT(ulLocationLength);
            READSHORT(ulLocationLength2);
            READSTRING(sException.strLocation, ulLocationLength2);
        }
        
        if(sException.ulOverrideFlags & ARO_BUSYSTATUS) {
            READLONG(sException.ulBusyStatus);
        }
        
        if(sException.ulOverrideFlags & ARO_ATTACHMENT) {
            READLONG(sException.ulAttachment);
        }
        
        if(sException.ulOverrideFlags & ARO_SUBTYPE) {
            READLONG(sException.ulSubType);
        }

        if(sException.ulOverrideFlags & ARO_APPTCOLOR) {
            READLONG(sException.ulAppointmentColor);
        }
        
        lstExceptions.push_back(sException);
    }
    
    bReadValid  = true;
    
    READLONG(ulReservedBlock1Size);
    READSTRING(strReservedBlock1, ulReservedBlock1Size);

    for (iterExceptions = lstExceptions.begin();
         iterExceptions != lstExceptions.end(); ++iterExceptions)
    {
        ExtendedException sExtendedException;
        unsigned int ulReservedBlock1Size;
        unsigned int ulReservedBlock2Size;
        unsigned int ulWideCharSubjectLength;
        unsigned int ulWideCharLocationLength;
        unsigned int ulChangeHighlightSize;
        
        if(ulWriterVersion2 >= 0x00003009) {
            READLONG(ulChangeHighlightSize);
            READLONG(sExtendedException.ulChangeHighlightValue);
            READSTRING(sExtendedException.strReserved, ulChangeHighlightSize-4);
        }
        
        READLONG(ulReservedBlock1Size);
        READSTRING(sExtendedException.strReservedBlock1, ulReservedBlock1Size);
        
        // According to the docs, these are condition depending on the OverrideFlags field. But that's wrong.
        if(iterExceptions->ulOverrideFlags & ARO_SUBJECT || iterExceptions->ulOverrideFlags & ARO_LOCATION) {       
            READLONG(sExtendedException.ulStartDateTime);
            READLONG(sExtendedException.ulEndDateTime);
            READLONG(sExtendedException.ulOriginalStartDate);
        }
        
        if(iterExceptions->ulOverrideFlags & ARO_SUBJECT) {
			std::string strBytes;
            READSHORT(ulWideCharSubjectLength);
            READSTRING(strBytes, ulWideCharSubjectLength * sizeof(short));
			TryConvert(converter, strBytes, ulWideCharSubjectLength * sizeof(short), "UCS-2LE", sExtendedException.strWideCharSubject);
        }

        if(iterExceptions->ulOverrideFlags & ARO_LOCATION) {
			std::string strBytes;
            READSHORT(ulWideCharLocationLength);
            READSTRING(strBytes, ulWideCharLocationLength * sizeof(short));
			TryConvert(converter, strBytes, ulWideCharLocationLength * sizeof(short), "UCS-2LE", sExtendedException.strWideCharLocation);
        }
        
        if(iterExceptions->ulOverrideFlags & ARO_SUBJECT || iterExceptions->ulOverrideFlags & ARO_LOCATION) {       
            READLONG(ulReservedBlock2Size);
            READSTRING(sExtendedException.strReservedBlock2, ulReservedBlock2Size);
        }
        
        lstExtendedExceptions.push_back(sExtendedException);
    }
	bExtended = true;

    READLONG(ulReservedBlock2Size);
    READSTRING(strReservedBlock2, ulReservedBlock2Size);

    DEBUGPRINT("%d Bytes left\n", ulLen - data.GetCursorPos());
    
    if(ulLen - data.GetCursorPos() != 0) {
        hr = MAPI_E_NOT_FOUND;
    }
    
exit:
    if (hr != hrSuccess && bReadValid) {
        hr = MAPI_W_ERRORS_RETURNED;

		// sync normal exceptions to extended exceptions, it those aren't present
		if (!bExtended) {
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

				lstExtendedExceptions.push_back(cEx);

				// clear for next exception
				cEx.strWideCharSubject.clear();
				cEx.strWideCharLocation.clear();
			}
		}
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
    HRESULT hr = hrSuccess;
    BinWriter data;
    std::vector<Exception>::const_iterator j = lstExceptions.begin();
    
    // There is one hard requirement: there must be as many Exceptions as there are ExtendedExceptions. Other
    // inconstencies are also bad, but we need at least that to even write the stream
    
    if(lstExceptions.size() != lstExtendedExceptions.size()) {
        hr = MAPI_E_CORRUPT_DATA;
        goto exit;
    }
    
    WRITESHORT(ulReaderVersion); 		WRITESHORT(ulWriterVersion);
    WRITESHORT(ulRecurFrequency);		WRITESHORT(ulPatternType);
    WRITESHORT(ulCalendarType);
    WRITELONG(ulFirstDateTime);
    WRITELONG(ulPeriod);
    WRITELONG(ulSlidingFlag);
    
    if(ulPatternType == 0x0000) {
        // No data
    } else if(ulPatternType == 0x0001) {
        WRITELONG(ulWeekDays);
    } else if(ulPatternType == 0x0002 || ulPatternType == 0x0004 || ulPatternType == 0x000a || ulPatternType == 0x000c) {
        WRITELONG(ulDayOfMonth);
    } else if(ulPatternType == 0x0003 || ulPatternType == 0x000b) {
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
			utf16string strWide = convert_to<utf16string>(i.strWideCharSubject);
			WRITESHORT(static_cast<ULONG>(strWide.size()));
			WRITESTRING(reinterpret_cast<const char *>(strWide.c_str()), static_cast<ULONG>(strWide.size()) * 2);
		}
		if (j->ulOverrideFlags & ARO_LOCATION) {
			utf16string strWide = convert_to<utf16string>(i.strWideCharLocation);
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
exit:    
    return hr;
}
