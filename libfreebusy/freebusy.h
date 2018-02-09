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

/**
@file
Free/busy Interface defines

@defgroup libfreebusy Freebusy library
@{

The Free/Busy API allows mail providers to provide free/busy status information for specified 
user accounts within a specified time range. The free/busy status of a block of time on a user's 
calendar is one of the following: out-of-office, busy, tentative, or free.

@par Create a Free/Busy Provider

To provide free/busy information to mail users, a mail provider creates a free/busy provider
and registers it with Outlook. The free/busy provider must implement the following interfaces.
Note that a number of members in these interfaces are not supported and must return the specified
return values. In particular, the Free/Busy API does not support write access to free/busy 
information and delegate access to accounts.

	- IFreeBusySupport \n
		This interface supports specification of interfaces that access free/busy data for 
		specified users. It uses FBUser to identify a user.
	- IFreeBusyData \n
		This interface gets and sets a time range for a given user and returns an interface
		for enumerating free/busy blocks of data within this time range. It uses relative time
		to get and set this time range.
	- IEnumFBBlock \n
		This interface supports accessing and enumerating free/busy blocks of data for a user
		within a time range. 

	@note
		An enumeration contains free/busy blocks that indicate the free/busy status of periods of
		time on a user's calendar, within a time range (specified by IFreeBusyData::EnumBlocks). 
		Items on a calendar, such as appointments and meeting requests, form blocks in the enumeration.
		Items adjacent to one another on the calendar having the same free/busy status are combined to
		form one single block. A free period of time on a calendar also forms a block. Therefore, no two
		consecutive blocks in an enumeration would have the same free/busy status. These blocks do not 
		overlap in time. When there are overlapping items on a calendar, Outlook merges these items to
		form non-overlapping free/busy blocks in the enumeration based on this order of precedence:
		out-of-office, busy, tentative.

@par To register the free/busy provider with Outlook, the mail provider should:

	-# Register the free/busy provider with COM, providing a CLSID that allows access to the provider's
		implementation of IFreeBusySupport.
	-# Let Outlook know that the free/busy provider exists by setting this key in the system registry: \n
		HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Office\\11.0\\Outlook\\SchedulingInformation\\FreeBusySupport
		
		For example, if the transport provider is SMTP, set the above key to the following data: \n
		- Name Type   Value \n
		- SMTP REG_SZ {CLSID for respective implementation of IFreeBusySupport} \n

		In this scenario, Outlook will co-create the COM class and use it to retrieve free/busy information
		for any SMTP mail users. To support an address book and transport provider that use an address entry
		type other than SMTP, change the Name accordingly. 
	
	\remarks
		During installation, free/busy providers should check if a registry setting for the the same address 
		entry type already exists. If it does, the free/busy provider should overwrite the current provider 
		for that address entry type, and restore to that provider when it uninstalls. However, if a user has 
		installed more than one free/busy provider for the same address entry type, the user should uninstall 
		these providers in the reverse order as installation (that is, always uninstall the latest provider), 
		otherwise the registry may point to a provider that has already been uninstalled.

@par API Components
	The Free/Busy API includes the following components:

	- Definitions
		- Constants for the Free/Busy API
	- Data Types
		- FBBlock_1
		- FBStatus
		- FBUser
		- sfbEvent
	- Interfaces
		- IEnumFBBlock
		- IFreeBusyData
		- IFreeBusySupport

*/

#ifndef FREEBUSY_INCLUDED
#define FREEBUSY_INCLUDED

#include <mapix.h>
#include <mapidefs.h>

/**
 * An enumeration for the free/busy status of free/busy blocks.
 * The free/busy status of a block of time determines how it is displayed on a 
 * calendar: Free, Busy, Tentative, or Out of Office.
 */
enum FBStatus {
	fbFree	= 0,					/**< Free */
	fbTentative = fbFree + 1,		/**< Tentative */
	fbBusy	= fbTentative + 1,		/**< Busy */
	fbOutOfOffice	= fbBusy + 1,	/**< Out Of Office */
	fbKopanoAllBusy = 1000			/**< Internal used */
};

/**
 * Defines a free/busy block of data. This is an item on a calendar represented by
 * an appointment or meeting request.
 */
struct FBBlock_1 {
	LONG m_tmStart;			/**< Start time for the block, expressed in relative time. */
	LONG m_tmEnd;			/**< End time for the block, expressed in relative time. */
	FBStatus m_fbstatus;	/**< Free/busy status for this block, indicating whether the user is 
								out-of-office, busy, tentative, or free, during the time period 
								between m_tmStart and m_tmEnd. */
};
typedef struct FBBlock_1 *LPFBBlock_1;

/** 
 * Identifies a user that may or may not have free/busy data available.
 */
struct FBUser {
	ULONG m_cbEid;			/**< The length of the entry ID of the mail user as represented by the IMailUser interface. */
	LPENTRYID m_lpEid;		/**< The entry ID of the mail user as represented by the IMailUser interface. */
	ULONG m_ulReserved;		/**< This parameter is reserved for Outlook internal use and is not supported. */
	LPWSTR m_pwszReserved;	/**< This parameter is reserved for Outlook internal use and is not supported.*/
};
typedef struct FBUser *LPFBUser;

/**
 * @interface IFreeBusyUpdate
 * Updates the freebusy data
 *
 * The interface IFreeBusyUpdate
 * Provided by: Free/busy provider 
 * Interface identifier: IID_IFreeBusyUpdate
 */
class IFreeBusyUpdate : public virtual IUnknown {
public:
	/**
	 * Unknown function, Possible reload the freebusydata ?
	 * @return This member must return S_OK
	 */
	virtual HRESULT Reload() = 0;

	/**
	 * Add freebusy blocks, May be called more than once successively
	 * Alternative name: AddAppt
	 * @param lpBlocks an array of free/busy blocks to publish
	 * @param nBlocks Number of freebusy blocks
	 */
	virtual HRESULT PublishFreeBusy(const FBBlock_1 *b, ULONG nblks) = 0;

	/**
	 * Unknown function, this member not supported
	 *
	 * @note The variables of the function are possible wrong should be like, 
	          int IFreeBusyUpdate__RemoveAppt(int,int,int,int);
	 * @return This member must return S_OK
	 */
	virtual HRESULT RemoveAppt() = 0;

	/**
	 * Remove all Freebusy data
	 * Alternative name: RemoveAllAppt
	 * @return This member must return S_OK
	 */
	virtual HRESULT ResetPublishedFreeBusy() = 0;

	/**
	 * Unknown function, this member not supported
	 *
	 * @note The variables of the function are possible wrong
	 * @return This member must return S_OK
	 */
	virtual HRESULT ChangeAppt() = 0;

	/**
	 * Save the freebusydata with time frame between the begintime and endtime.
	 */
	virtual HRESULT SaveChanges(const FILETIME &start, const FILETIME &end) = 0;

	/**
	 * Unknown function, this member not supported
	 *
	 * @note The variables of the function are possible wrong
	 * @return This member must return S_OK
	 */
	virtual HRESULT GetFBTimes() = 0;

	/**
	 * Unknown function, this member not supported
	 *
	 * @note The variables of the function are possible wrong
	 * @return This member must return S_OK
	 */
	virtual HRESULT Intersect() = 0;
};

/**
 * @interface IEnumFBBlock
 * Supports accessing and enumerating free/busy blocks of data for a user within a time range.
 *
 * Class: IEnumFBBlock
 * Inherits from: IUnknown
 * Provided by: Free/busy provider 
 * Interface identifier: IID_IEnumFBBlock
 *
 * An enumeration contains free/busy blocks of data that do not overlap in time. When 
 * there are overlapping items on a calendar, Outlook merges these items to form 
 * non-overlapping free/busy blocks in the enumeration based on this order of
 * precedence: out-of-office, busy, tentative.
 * 
 * A free/busy provider obtains this interface and the enumeration for a time range for
 * a user through IFreeBusyData. 
 * 
 */
class IEnumFBBlock : public virtual IUnknown {
public:
	/**
	 * Gets the next specified number of blocks of free/busy data in an enumeration.
	 *
     * @param[in] celt Number of free/busy data blocks in pblk to retrieve.
	 * @param[in] pblk Pointer to an array of free/busy blocks. The array is allocated a size of celt. 
		The requested free/busy blocks are returned in this array.
	 * @param[out] pcfetch Number of free/busy blocks actually returned in pblk.
	 *
	 * @retval S_OK The requested number of blocks has been returned.
	 * @retval S_FALSE The requested number of blocks has not been returned.
	 */
	virtual HRESULT Next(LONG celt, FBBlock_1 *pblk, LONG *pcfetch) = 0;


	/**
	 * Skips a specified number of blocks of free/busy data.
	 *
	 * @param[in] celt Number of free/busy blocks to skip.
	 *
	 * @retval S_OK The requested number of blocks has been skiped.
	 */
	virtual HRESULT Skip(LONG celt) = 0;


	/**
	 * Resets the enumerator by setting the cursor to the beginning.
	 */
	virtual HRESULT Reset() = 0;


	/**
	 * Creates a copy of the enumerator,  using the same time restriction 
	 * but setting the cursor to the beginning of the enumerator.
	 *
	 * @param[out] ppclone Pointer to pointer to the copy of IEnumFBBlock interface.
	 *
	 * @retval E_OUTOFMEMORY There is insufficient memory for making the copy.
	 
	 */
	virtual HRESULT Clone(IEnumFBBlock **ppclone) = 0;


	/**
	 * Restricts the enumeration to a specified time period.
	 *
	 * @param[in] ftmStart Start time to restrict enumeration. 
	 * @param[in] ftmEnd End time to restrict enumeration.
	 *
	 * @note This method also resets the enumeration.
	 */
	virtual HRESULT Restrict(const FILETIME &start, const FILETIME &end) = 0;
};


/**
 * @interface IFreeBusyData
 * For a given user, gets and sets a time range and returns an interface for 
 * enumerating free/busy blocks of data within this time range.
 *
 * Class: IFreeBusyData
 * Inherits from: IUnknown 
 * Provided by: Free/busy provider
 * Interface identifier: IID_IFreeBusyData 
 *
 * Most of the members in this interface are placeholders reserved for the internal 
 * use of Outlook and are subject to change. Free/busy providers must implement them only 
 * as specified, returning only the specified return values.
 */
class IFreeBusyData : public virtual IUnknown {
public:
	
	/**
	 * This member not supported must return E_NOTIMPL.
	 */
	virtual HRESULT Reload(void*) = 0;

	/**
	 * Gets an interface for a user that enumerates free/busy blocks of data within a 
	 * specified time range.
	 *
	 * @param[out] ppenumfb Interface to enumerate free/busy blocks.
	 * @param[in] ftmStart Start time for enumeration. It is expressed in FILETIME.
	 * @param[in] ftmEnd End time for enumeration. It is expressed in FILETIME.
	 *
	 * \remarks
	 * Used to indicate the time range of calendar items for which to retrieve details.
	 * The values of ftmStart and ftmEnd are cached and returned in a subsequent call of 
	 * IFreeBusyData::GetFBPublishRange.
	 *
	 * A free/busy provider can also subsequently use the returned IEnumFBBlock interface
	 * to access the enumeration.
	 */
	virtual HRESULT EnumBlocks(IEnumFBBlock **ppenumfb, const FILETIME &start, const FILETIME &end) = 0;

	/**
	 * This member not supported must return E_NOTIMPL.
	 */
	virtual HRESULT Merge(void *) = 0;

	/**
	 * This member not supported must return E_NOTIMPL.
	 */
	virtual HRESULT GetDelegateInfo(void *) = 0;

	/**
	 * This member not supported must return S_OK.
	 */
	virtual HRESULT FindFreeBlock(LONG, LONG, LONG, BOOL, LONG, LONG, LONG, FBBlock_1 *) = 0;

	/**
	 * This member not supported must return E_NOTIMPL.
	 */
	virtual HRESULT InterSect(void *, LONG, void *) = 0;

	/**
	 * Sets the range of time for an enumeration of free/busy block of data for a user.
	 *
	 * @param[in] rtmStart
	 *			A relative time value for the start of free/busy information. 
	 *			This value is the number of minutes since January 1, 1601.
	 * @param[in] rtmEnd
	 *			A relative time value for the end of free/busy information. 
	 *			This value is the number of minutes since January 1, 1601.
	 *
	 * \remarks
	 *	Used to indicate the time range of calendar items for which to retrieve details.
	 *	The values of ftmStart and ftmEnd are cached and returned in a subsequent call of 
	 *	IFreeBusyData::GetFBPublishRange.
	 */
	virtual HRESULT SetFBRange(LONG rtmStart, LONG rtmEnd) = 0;

	/**
	 * This member not supported must return E_NOTIMPL.
	 */
	virtual HRESULT NextFBAppt(void *, ULONG, void *, ULONG, void *, void *) = 0;


	/**
	 * Gets a preset time range for an enumeration of free/busy blocks of data for a user.
	 *
	 * @param[out] prtmStart 
	 *		A relative time value for the start of free/busy information.
	 *		This value is the number of minutes since January 1, 1601.
	 * @param[out] prtmEnd 
	 *		A relative time value for the end of free/busy information.
	 *		This value is the number of minutes since January 1, 1601.
	 *
	 * \remarks
	 *	A free/busy provider calls IFreeBusyData::EnumBlocks or IFreeBusyData::SetFBRange 
	 *	to set the time range for an enumeration. IFreeBusyData::GetFBPublishRange must
	 *	return the cached values for the time range set by the most recent call for
	 *	IFreeBusyData::EnumBlocks or IFreeBusyData::SetFBRange.
	 */
	virtual HRESULT GetFBPublishRange(LONG *prtmStart, LONG *prtmEnd) = 0;
};

/**
 * @interface IFreeBusySupport
 * Supports specification of interfaces that access free/busy data for specified users. 
 *
 * Class: IFreeBusySupport
 * Inherits from: IUnknown 
 * Provided by: Free/busy provider 
 * Interface identifier: IID_IFreeBusySupport 
 * 
 * \remarks
 * 	Most of the members in this interface are placeholders reserved for the internal
 *	use of Outlook and are subject to change. Free/busy providers must implement them 
 *	only as specified, returning only the specified return values.
 *
 */
class IFreeBusySupport : public virtual IUnknown {
public:

	/**
	 * Initialize the freebusy support object for further uses of the interface.
	 * 
	 * The first call to get the information to communicate with MAPI
	 * The freebusysupport holds the session and store to use it in other functions
	 *
	 * @param[in] lpMAPISession
	 *		A sessionobject to open a store in other functions of IFreeBusySupport interface
	 * @param[in] lpMsgStore
	 *		Userstore to save freebusy data. Passing NULL results in the freebusydata is readonly.
	 * @param[in] bStore
	 *		Unknown data, is true if lpMsgStore isn't NULL
	 *	
	 */
	virtual HRESULT Open(IMAPISession *lpMAPISession, IMsgStore *lpMsgStore, BOOL bStore) = 0;

	/**
	 * Close the free/busy support object data.
	 * 
	 * It will release session and store which are initialized by IFreeBusySupport::Open
	 * 
	 */
	virtual HRESULT Close() = 0;

	/**
	 * Returns, for each specified user, an interface for enumerating free/busy blocks
	 * of data within a time range.
	 *
	 * @param[in] cMax 
	 *		The number of IFreeBusyData interfaces to return.
	 * @param[in] rgfbuser 
	 *		The array of free/busy users to retrieve data for.
	 * @param[in,out] prgfbdata 
	 *		The array of IFreeBusyData interfaces corresponding to the rgfbuser array
	 *		of FBUser structures.
	 *
	 *		@note This array of pointers is allocated by the caller and freed by the caller. 
	 *			  The actual interfaces pointed to are released when the caller is done with them. 
	 * @param[out] phrStatus 
	 *		The array of HRESULT results for retrieving each corresponding IFreeBusyData
	 *		interface. The value may be NULL. A result is set to S_OK if corresponding 
	 *		prgfbdata is valid. 
	 * @param[out] pcRead 
	 *		The actual number of users for which an IFreeBusyData interface has been found.
	 *
	 * @retval S_OK 
	 *				The call is successful.
	 */
	virtual HRESULT LoadFreeBusyData(ULONG cMax, FBUser *rgfbuser, IFreeBusyData **prgfbdata, HRESULT *phrStatus, ULONG *pcRead) = 0;

	/**
	 * This member is a placeholder and is not supported, but hacked now. 
	 *
	 * @param[in] cUsers
	 *		The number of users in lpUsers
	 * @param[in] lpUsers
	 *		The array of user information
	 * @param[out] lppFBUpdate
	 *		Should return array of IFreeBusyUpdate interfaces for each user.
	 *		
	 *		@note This array of pointers is allocated by the caller and freed by the caller. 
	 *			  The actual interfaces pointed to are released when the caller is done with them. 
	 * @param[out] lpcFBUpdate
	 *		actual returned interfaces in lppFBUpdate
	 * @param lpData4
	 *			unknown param (always NULL? )
	 *
	 * @retval S_OK
	 *			The call is successful.
	 * @retval E_NOTIMPL 
	 *			The is not supported.
	 *
	 * @todo research for lpDate4
	 */
	virtual HRESULT LoadFreeBusyUpdate(ULONG cUsers, FBUser *lpUsers, IFreeBusyUpdate **lppFBUpdate, ULONG *lpcFBUpdate, void *lpData4) = 0;

	/**
	 * This member not supported must return S_OK.
	 */
	virtual HRESULT CommitChanges() = 0;

	/**
	 * This member not supported must return E_NOTIMPL.
	 */
	virtual HRESULT GetDelegateInfo(FBUser, void *) = 0;

	/**
	 * This member not supported must return E_NOTIMPL.
	 */
	virtual HRESULT SetDelegateInfo(void *) = 0;

	/**
	 * This member not supported must return E_NOTIMPL.
	 */
	virtual HRESULT AdviseFreeBusy(void *) = 0;

	/**
	 * This member not supported must return E_NOTIMPL.
	 */
	virtual HRESULT Reload(void *) = 0;

	/**
	 * This member not supported must return E_NOTIMPL.
	 */
	virtual HRESULT GetFBDetailSupport(void **, BOOL) = 0;

	/**
	 * This member not supported must return E_NOTIMPL.
	 */
	virtual HRESULT HrHandleServerSched(void *) = 0;

	/**
	 * This member not supported must return E_NOTIMPL.
	 */
	virtual HRESULT HrHandleServerSchedAccess() = 0;

	/**
	 * This member not supported must return FALSE.
	 */
	virtual BOOL FShowServerSched(BOOL) = 0;

	/**
	 * This member not supported must return S_OK.
	 */
	virtual HRESULT HrDeleteServerSched() = 0;

	/**
	 * This member not supported must return E_NOTIMPL.
	 */
	virtual HRESULT GetFReadOnly(void *) = 0;

	/**
	 * This member not supported must return E_NOTIMPL.
	 *
	 * @note The guid of lpData is 
	 *		DEFINE_GUID(IID_I????, 0x00067069, 0x0, 0x0, 0xc0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x46);//{00067069-0000-0000-C000-000000000046}
	 *      lpData vtable are total 24 functions
	 */
	virtual HRESULT SetLocalFB(void *) = 0;

	/**
	 * This member not supported must return E_NOTIMPL.
	 */
	virtual HRESULT PrepareForSync() = 0;

	/**
	 * This member not supported must return E_NOTIMPL.
	 */
	virtual HRESULT GetFBPublishMonthRange(void *) = 0;

	/**
	 * This member not supported must return E_NOTIMPL.
	 */
	virtual HRESULT PublishRangeChanged() = 0;

	/**
	 * This member not supported must return E_NOTIMPL.
	 *
	 * Alternative name: Clean
	 */
	virtual HRESULT CleanTombstone() = 0;

	/**
	 * Get delegate information of a user.
	 *
	 * @param[in] sFBUser The user information
	 * @param[out] lpulStatus A 'unknown struct' with delegate information
	 * @param[out] prtmStart The start time of the publish range (should be ULONG)
	 * @param[out] prtmEnd The end time of the publish range (should be ULONG)
	 * 
	 * @retval E_NOTIMPL 
	 *				Not supported, client should send a meeting request
	 *
	 * @todo change type of prtmStart and prtmEnd from unsigned int to LONG
	 * @todo change type lpulStatus to void or the right struct
	 */
	virtual HRESULT GetDelegateInfoEx(FBUser sFBUser, unsigned int *lpulStatus, unsigned int *prtmStart, unsigned int *prtmEnd) = 0;

	/**
	 * This member not supported must return E_NOTIMPL.
	 */
	virtual HRESULT PushDelegateInfoToWorkspace() = 0;
};

/**
 * @interface IFreeBusySupportOutlook2000
 * Supports specification of interfaces that access free/busy data for specified users. used in Outlook 2000
 *
 * Class: IFreeBusySupportOutlook2000
 * Inherits from: IUnknown 
 * Provided by: Free/busy provider 
 * Interface identifier: IID_IFreeBusySupport 
 * 
 * \remarks
 * 	Most of the members in this interface are placeholders reserved for the internal
 *	use of Outlook and are subject to change. Free/busy providers must implement them 
 *	only as specified, returning only the specified return values.
 *
 * \warning
 *	Missing call is CleanTombstone. This is not support in outlook 2000
 *
 */
class IFreeBusySupportOutlook2000 : public virtual IUnknown {
public:

	/*! @copydoc IFreeBusySupport::Open */
	virtual HRESULT Open(IMAPISession *lpMAPISession, IMsgStore *lpMsgStore, BOOL bStore) = 0;

	/*! @copydoc IFreeBusySupport::Close */
	virtual HRESULT Close() = 0;

	/*! @copydoc IFreeBusySupport::LoadFreeBusyData */
	virtual HRESULT LoadFreeBusyData(ULONG cMax, FBUser *rgfbuser, IFreeBusyData **prgfbdata, HRESULT *phrStatus, ULONG *pcRead) = 0;

	/*! @copydoc IFreeBusySupport::LoadFreeBusyUpdate */
	virtual HRESULT LoadFreeBusyUpdate(ULONG cUsers, FBUser *lpUsers, IFreeBusyUpdate **lppFBUpdate, ULONG *lpcFBUpdate, void *lpData4) = 0;

	/*! @copydoc IFreeBusySupport::CommitChanges */
	virtual HRESULT CommitChanges() = 0;

	/*! @copydoc IFreeBusySupport::GetDelegateInfo */
	virtual HRESULT GetDelegateInfo(FBUser, void *) = 0;

	/*! @copydoc IFreeBusySupport::SetDelegateInfo */
	virtual HRESULT SetDelegateInfo(void *) = 0;

	/*! @copydoc IFreeBusySupport::AdviseFreeBusy */
	virtual HRESULT AdviseFreeBusy(void *) = 0;

	/*! @copydoc IFreeBusySupport::Reload */
	virtual HRESULT Reload(void *) = 0;

	/*! @copydoc IFreeBusySupport::GetFBDetailSupport */
	virtual HRESULT GetFBDetailSupport(void **, BOOL) = 0;

	/*! @copydoc IFreeBusySupport::HrHandleServerSched */
	virtual HRESULT HrHandleServerSched(void *) = 0;

	/*! @copydoc IFreeBusySupport::HrHandleServerSchedAccess */
	virtual HRESULT HrHandleServerSchedAccess() = 0;

	/*! @copydoc IFreeBusySupport::FShowServerSched */
	virtual BOOL FShowServerSched(BOOL) = 0;

	/*! @copydoc IFreeBusySupport::HrDeleteServerSched */
	virtual HRESULT HrDeleteServerSched() = 0;

	/*! @copydoc IFreeBusySupport::GetFReadOnly */
	virtual HRESULT GetFReadOnly(void *) = 0;

	/*! @copydoc IFreeBusySupport::SetLocalFB */
	virtual HRESULT SetLocalFB(void *) = 0;

	/*! @copydoc IFreeBusySupport::PrepareForSync */
	virtual HRESULT PrepareForSync() = 0;

	/*! @copydoc IFreeBusySupport::GetFBPublishMonthRange */
	virtual HRESULT GetFBPublishMonthRange(void *) = 0;

	/*! @copydoc IFreeBusySupport::PublishRangeChanged */
	virtual HRESULT PublishRangeChanged() = 0;

	/*! 
	* @copydoc IFreeBusySupport::CleanTombstone
	*
	* Not supported in outlook 2000
	*/
	//virtual HRESULT CleanTombstone() = 0;

	/*! @copydoc IFreeBusySupport::GetDelegateInfoEx */
	virtual HRESULT GetDelegateInfoEx(FBUser sFBUser, unsigned int *lpulStatus, unsigned int *prtmStart, unsigned int *prtmEnd) = 0;

	/*! @copydoc IFreeBusySupport::PushDelegateInfoToWorkspace */
	virtual HRESULT PushDelegateInfoToWorkspace() = 0;
};

namespace KC {

/**
 * Extends the free/busy block of data. It also stores the basedate of occurrence
 * for exceptions. For normal occurrences base date is same as start date
 */
struct OccrInfo {
	FBBlock_1 fbBlock;
	time_t tBaseDate;
};

} /* namespace */

#endif // FREEBUSY_INCLUDED

/** @} */
