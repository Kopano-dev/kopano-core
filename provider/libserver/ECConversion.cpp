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

#include <kopano/platform.h>

#include "soapH.h"

#include "SOAPUtils.h"
#include "ECDatabase.h"
#include <kopano/stringutil.h>
#include <sstream>

#include "ECConversion.h"

using namespace std;

// Convert search criteria from zarafa-5.2x to zarafa-6 format
ECRESULT ConvertSearchCriteria52XTo6XX(ECDatabase *lpDatabase, char* lpData, struct searchCriteria **lppNewSearchCriteria)
{
	if (lpDatabase == NULL || lpData == NULL || lppNewSearchCriteria == NULL)
		return KCERR_INVALID_PARAMETER;

	ECRESULT er = erSuccess;
	
	DB_ROW lpDBRow = NULL;
	DB_RESULT lpDBResult = NULL;
	DB_LENGTHS lpDBLenths = NULL;
	std::string strQuery;
	unsigned int i;

	struct soap xmlsoap;
	struct searchCriteria *lpNewSearchCriteria = NULL;
	struct searchCriteria52X *lpSearchCriteria = NULL;

	std::string xmldata(lpData);
	std::istringstream xml(xmldata);

	// We use the soap serializer / deserializer to store the data
	soap_set_mode(&xmlsoap, SOAP_XML_TREE | SOAP_C_UTFSTRING);

	lpSearchCriteria = new struct searchCriteria52X;

	// Workaround for gsoap bug in which it does a set_mode on FD 0 (stdin) which causes soap_begin_recv to hang
	// until input is received
	xmlsoap.recvfd = -1;
	xmlsoap.is = &xml;
	soap_default_searchCriteria52X(&xmlsoap, lpSearchCriteria);
	if (soap_begin_recv(&xmlsoap) != 0) {
		er = KCERR_NETWORK_ERROR;
		goto exit;
	}
	soap_get_searchCriteria52X(&xmlsoap, lpSearchCriteria, "SearchCriteria", NULL);

	// We now have the object, allocated by xmlsoap object,
	if (soap_end_recv(&xmlsoap) != 0) {
		er = KCERR_NETWORK_ERROR;
		goto exit;
	}

	lpNewSearchCriteria = new struct searchCriteria;
	memset(lpNewSearchCriteria, 0, sizeof(struct searchCriteria));

	// Do backward-compatibility fixup
	if(lpSearchCriteria->lpRestrict)
	{
		// Copy the restriction
		er = CopyRestrictTable(NULL, lpSearchCriteria->lpRestrict, &lpNewSearchCriteria->lpRestrict);
		if (er != erSuccess)
			goto exit;
	}

	// Flags
	lpNewSearchCriteria->ulFlags = lpSearchCriteria->ulFlags;

	// EntryidList
	if (lpSearchCriteria->lpFolders && lpSearchCriteria->lpFolders->__size > 0 && lpSearchCriteria->lpFolders->__ptr != NULL)
	{

		lpNewSearchCriteria->lpFolders = s_alloc<struct entryList>(NULL);
		lpNewSearchCriteria->lpFolders->__ptr = s_alloc<entryId>(NULL, lpSearchCriteria->lpFolders->__size);
		lpNewSearchCriteria->lpFolders->__size = 0;

		memset(lpNewSearchCriteria->lpFolders->__ptr, 0, sizeof(entryId) * lpSearchCriteria->lpFolders->__size);

		// Get them from the database
		strQuery = "SELECT val_binary FROM indexedproperties WHERE tag=0x0FFF AND hierarchyid IN ("; //PR_ENTRYID
		for (i = 0; i < lpSearchCriteria->lpFolders->__size; ++i) {
			if (i != 0)strQuery+= ",";
			strQuery+= stringify(lpSearchCriteria->lpFolders->__ptr[i]);
		}
		strQuery+= ")";

		er = lpDatabase->DoSelect(strQuery, &lpDBResult);
		if(er != erSuccess)
			goto exit;

		while ((lpDBRow = lpDatabase->FetchRow(lpDBResult)))
		{
			lpDBLenths = lpDatabase->FetchRowLengths(lpDBResult);

			if (lpDBRow[0] == NULL)
				continue; // Skip row, old folder

			i = lpNewSearchCriteria->lpFolders->__size;
			
			lpNewSearchCriteria->lpFolders->__ptr[i].__ptr = s_alloc<unsigned char>(NULL, (unsigned int) lpDBLenths[0]);
			memcpy(lpNewSearchCriteria->lpFolders->__ptr[i].__ptr, (unsigned char*) lpDBRow[0], lpDBLenths[0]);

			lpNewSearchCriteria->lpFolders->__ptr[i].__size = (unsigned int) lpDBLenths[0];
			++lpNewSearchCriteria->lpFolders->__size;
		}
		
	} //if (lpSearchCriteria...)

	soap_destroy(&xmlsoap);
	soap_end(&xmlsoap);
	soap_done(&xmlsoap);

	*lppNewSearchCriteria = lpNewSearchCriteria;
exit:
	if (er != erSuccess && lpNewSearchCriteria)
		FreeSearchCriteria(lpNewSearchCriteria);

	delete lpSearchCriteria;

	if (lpDBResult)
		lpDatabase->FreeResult(lpDBResult);

	return er;
}

SOAP_FMAC3 void SOAP_FMAC4 soap_default_searchCriteria52X(struct soap *soap, struct searchCriteria52X *a)
{
	(void)soap; (void)a; /* appease -Wall -Werror */
	a->lpRestrict = NULL;
	a->lpFolders = NULL;
	soap_default_unsignedInt(soap, &a->ulFlags);
}

SOAP_FMAC3 struct searchCriteria52X * SOAP_FMAC4 soap_get_searchCriteria52X(struct soap *soap, struct searchCriteria52X *p, const char *tag, const char *type)
{
	if ((p = soap_in_searchCriteria52X(soap, tag, p, type)))
		soap_getindependent(soap);
	return p;
}

SOAP_FMAC3 struct searchCriteria52X * SOAP_FMAC4 soap_in_searchCriteria52X(struct soap *soap, const char *tag, struct searchCriteria52X *a, const char *type)
{
	short soap_flag_lpRestrict = 1, soap_flag_lpFolders = 1, soap_flag_ulFlags = 1;
	if (soap_element_begin_in(soap, tag, 0, type))
		return NULL;
	if (*soap->type && soap_match_tag(soap, soap->type, type))
	{	soap->error = SOAP_TYPE;
		return NULL;
	}
	a = (struct searchCriteria52X *)soap_id_enter(soap, soap->id, a, SOAP_TYPE_searchCriteria, sizeof(struct searchCriteria52X), 0, NULL, NULL, NULL);
	if (!a)
		return NULL;
	soap_default_searchCriteria52X(soap, a);
	if (soap->body && !*soap->href)
	{
		for (;;)
		{	soap->error = SOAP_TAG_MISMATCH;
			if (soap_flag_lpRestrict && soap->error == SOAP_TAG_MISMATCH)
				if (soap_in_PointerTorestrictTable(soap, "lpRestrict", &a->lpRestrict, "restrictTable")) {
					--soap_flag_lpRestrict;
					continue;
				}
			if (soap_flag_lpFolders && soap->error == SOAP_TAG_MISMATCH)
				if (soap_in_PointerToentryList52X(soap, "lpFolders", &a->lpFolders, "entryList")) {
					--soap_flag_lpFolders;
					continue;
				}
			if (soap_flag_ulFlags && soap->error == SOAP_TAG_MISMATCH)
				if (soap_in_unsignedInt(soap, "ulFlags", &a->ulFlags, "xsd:unsignedInt")) {
					--soap_flag_ulFlags;
					continue;
				}
			if (soap->error == SOAP_TAG_MISMATCH)
				soap->error = soap_ignore_element(soap);
			if (soap->error == SOAP_NO_TAG)
				break;
			if (soap->error)
				return NULL;
		}
		if ((soap->mode & SOAP_XML_STRICT) && soap_flag_ulFlags > 0)
		{	soap->error = SOAP_OCCURS;
			return NULL;
		}
		if (soap_element_end_in(soap, tag))
			return NULL;
	}
	else
	{
#if GSOAP_VERSION >= 20824
		a = static_cast<struct searchCriteria52X *>(soap_id_forward(soap,
		    soap->href, reinterpret_cast<void **>(a), 0,
		    SOAP_TYPE_searchCriteria, 0, sizeof(*a), 0, NULL, NULL));
#else
		a = static_cast<struct searchCriteria52X *>(soap_id_forward(soap,
		    soap->href, reinterpret_cast<void **>(a), 0,
		    SOAP_TYPE_searchCriteria, 0, sizeof(*a), 0, NULL));
#endif
		if (soap->body && soap_element_end_in(soap, tag))
			return NULL;
	}
	return a;
}

SOAP_FMAC3 struct entryList52X ** SOAP_FMAC4 soap_in_PointerToentryList52X(struct soap *soap, const char *tag, struct entryList52X **a, const char *type)
{
	if (soap_element_begin_in(soap, tag, 1, type))
		return NULL;
	if (!a)
		if (!(a = (struct entryList52X **)soap_malloc(soap, sizeof(struct entryList52X *))))
			return NULL;
	*a = NULL;
	if (!soap->null && *soap->href != '#')
	{	soap_revert(soap);
		if (!(*a = soap_in_entryList52X(soap, tag, *a, type)))
			return NULL;
	}
	else
	{
#if GSOAP_VERSION >= 20824
		a = static_cast<struct entryList52X **>(soap_id_lookup(soap,
		    soap->href, reinterpret_cast<void **>(a),
		    SOAP_TYPE_entryList, sizeof(*a), 0, NULL));
#else
		a = static_cast<struct entryList52X **>(soap_id_lookup(soap,
		    soap->href, reinterpret_cast<void **>(a),
		    SOAP_TYPE_entryList, sizeof(*a), 0));
#endif
		if (soap->body && soap_element_end_in(soap, tag))
			return NULL;
	}
	return a;
}

SOAP_FMAC3 struct entryList52X * SOAP_FMAC4 soap_in_entryList52X(struct soap *soap, const char *tag, struct entryList52X *a, const char *type)
{
	short soap_flag___ptr = 1;
	if (soap_element_begin_in(soap, tag, 0, type))
		return NULL;
	if (*soap->type && soap_match_tag(soap, soap->type, type))
	{	soap->error = SOAP_TYPE;
		return NULL;
	}
	a = (struct entryList52X *)soap_id_enter(soap, soap->id, a, SOAP_TYPE_entryList, sizeof(struct entryList52X), 0, NULL, NULL, NULL);
	if (!a)
		return NULL;
	soap_default_entryList52X(soap, a);
	if (soap->body && !*soap->href)
	{
		for (;;)
		{	soap->error = SOAP_TAG_MISMATCH;
			if (soap_flag___ptr && soap->error == SOAP_TAG_MISMATCH)
			{	unsigned int *p;
				soap_alloc_block(soap);
				for (a->__size = 0; !soap_element_begin_in(soap, "item", 1, type); ++a->__size) {
					p = (unsigned int *)soap_push_block(soap, NULL, sizeof(unsigned int));
					soap_default_unsignedInt(soap, p);
					soap_revert(soap);
					if (!soap_in_unsignedInt(soap, "item", p, "xsd:unsignedInt"))
						break;
					soap_flag___ptr = 0;
				}
				a->__ptr = (unsigned int *)soap_save_block(soap, NULL, NULL, 1);
				if (!soap_flag___ptr && soap->error == SOAP_TAG_MISMATCH)
					continue;
			}
			if (soap->error == SOAP_TAG_MISMATCH)
				soap->error = soap_ignore_element(soap);
			if (soap->error == SOAP_NO_TAG)
				break;
			if (soap->error)
				return NULL;
		}
		if (soap_element_end_in(soap, tag))
			return NULL;
	}
	else
	{
#if GSOAP_VERSION >= 20824
		a = static_cast<struct entryList52X *>(soap_id_forward(soap,
		    soap->href, reinterpret_cast<void **>(a), 0,
		    SOAP_TYPE_entryList, 0, sizeof(*a), 0, NULL, NULL));
#else
		a = static_cast<struct entryList52X *>(soap_id_forward(soap,
		    soap->href, reinterpret_cast<void **>(a), 0,
		    SOAP_TYPE_entryList, 0, sizeof(*a), 0, NULL));
#endif
		if (soap->body && soap_element_end_in(soap, tag))
			return NULL;
	}
	return a;
}

SOAP_FMAC3 void SOAP_FMAC4 soap_default_entryList52X(struct soap *soap, struct entryList52X *a)
{
	(void)soap; (void)a; /* appease -Wall -Werror */
	a->__size = 0;
	a->__ptr = NULL;
}
