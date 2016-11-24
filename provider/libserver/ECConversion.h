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

#ifndef ECCONVERSION_H
#define ECCONVERSION_H

/* entryList */
struct entryList52X {
	unsigned int __size;	/* sequence of elements <item> */
	unsigned int *__ptr;
};

/* searchCriteria */
struct searchCriteria52X {
	struct restrictTable *lpRestrict;	/* optional element of type restrictTable */
	struct entryList52X *lpFolders;	/* optional element of type entryList */
	unsigned int ulFlags;	/* required element of type xsd:unsignedInt */
};

SOAP_FMAC3 void SOAP_FMAC4 soap_default_searchCriteria52X(struct soap *soap, struct searchCriteria52X *a);
SOAP_FMAC3 struct searchCriteria52X * SOAP_FMAC4 soap_get_searchCriteria52X(struct soap *soap, struct searchCriteria52X *p, const char *tag, const char *type);
SOAP_FMAC3 struct searchCriteria52X * SOAP_FMAC4 soap_in_searchCriteria52X(struct soap *soap, const char *tag, struct searchCriteria52X *a, const char *type);
SOAP_FMAC3 struct entryList52X ** SOAP_FMAC4 soap_in_PointerToentryList52X(struct soap *soap, const char *tag, struct entryList52X **a, const char *type);
SOAP_FMAC3 struct entryList52X * SOAP_FMAC4 soap_in_entryList52X(struct soap *soap, const char *tag, struct entryList52X *a, const char *type);
SOAP_FMAC3 void SOAP_FMAC4 soap_default_entryList52X(struct soap *soap, struct entryList52X *a);

namespace KC {

ECRESULT ConvertSearchCriteria52XTo6XX(ECDatabase *lpDatabase, char* lpData, struct searchCriteria **lppNewSearchCriteria);

} /* namespace */

#endif // #ifndef ECCONVERSION_H
