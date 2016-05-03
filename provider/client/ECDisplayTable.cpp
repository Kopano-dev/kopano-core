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
#include "ECDisplayTable.h"

#include <kopano/ECGetText.h>

#include <kopano/CommonUtil.h>

#include "Mem.h"
#include <kopano/ECMemTable.h>

#if defined(_WIN32) && !defined(WINCE)
	#include "dialogdefs.h"
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// This function is NOT used, but is used as dummy for the xgettext parser so it
// can find the translation strings. **DO NOT REMOVE THIS FUNCTION**
static UNUSED_VAR void dummy(void)
{
	LPCTSTR a UNUSED_VAR;

	/* User General tab */
	a = _("General");
	a = _("Name");
	a = _("First:");
	a = _("Initials:");
	a = _("Last:");
	a = _("Display:");
	a = _("Alias:");
	a = _("Address:");
	a = _("City:");
	a = _("State:");
	a = _("Zip code:");
	a = _("Country/Region:");
	a = _("Title:");
	a = _("Company:");
	a = _("Department:");
	a = _("Office:");
	a = _("Assistant:");
	a = _("Phone:");

	/* User Phone/Notes tab */
	a = _("Phone/Notes");
	a = _("Phone numbers");
	a = _("Business:");
	a = _("Business 2:");
	a = _("Fax:");
	a = _("Assistant:");
	a = _("Home:");
	a = _("Home 2:");
	a = _("Mobile:");
	a = _("Pager:");
	a = _("Notes:");

	/* User Organization tab */
	a = _("Organization");
	a = _("Manager:");
	a = _("Direct reports:");

	/* User Member Of tab */
	a = _("Member Of");
	a = _("Group membership:");

	/* User E-mail Addresses tab */
	a = _("E-mail Addresses");
	a = _("E-mail addresses:");

	/* Distlist General tab */
	a = _("General");
	a = _("Display name:");
	a = _("Alias name:");
	a = _("Owner:");
	a = _("Notes:");
	a = _("Members");
	a = _("Modify members...");

	/* Distlist Member Of tab */
	a = _("Member Of");
	a = _("Group membership:");
}

#if defined(WIN32) && !defined(WINCE)
void TranslateDisplayTable(ITableData *lpTableData)
{
	HRESULT hr;
	LPSRow lpsRow = NULL;
	LPSPropValue lpPropType = NULL; // non-free
	LPSPropValue lpPropControl = NULL; // non-free
	int n = 0;
	unsigned int ulClientVersion = 0;

	hr = GetClientVersion(&ulClientVersion);
	if (hr != hrSuccess)
		return;

	while(TRUE) {

		lpsRow = NULL;
		hr = lpTableData->HrEnumRow(n++, &lpsRow);

		if(hr != hrSuccess || lpsRow == NULL)
			break;

		lpPropType = PpropFindProp(lpsRow->lpProps, lpsRow->cValues, PR_CONTROL_TYPE);
		lpPropControl = PpropFindProp(lpsRow->lpProps, lpsRow->cValues, PR_CONTROL_STRUCTURE);

		if(lpPropType && lpPropControl && lpPropType->Value.ul == DTCT_LABEL) {
			DTBLLABEL *lpLabel = (DTBLLABEL *)lpPropControl->Value.bin.lpb;

			// Found a label, translate the text through gettext
			char *szTranslated = _A((char *)lpLabel + lpLabel->ulbLpszLabelName);

			// re-create a DTBLLABEL structure with the translated string
			lpLabel = (DTBLLABEL *)new char [ sizeof(DTBLLABEL) + strlen(szTranslated) + 1 ];
			lpLabel->ulbLpszLabelName = sizeof(DTBLLABEL);
			lpLabel->ulFlags = 0; // no unicode
			strcpy(((char *)lpLabel) + sizeof(DTBLLABEL), szTranslated);

			lpPropControl->Value.bin.lpb = (LPBYTE)lpLabel;
			lpPropControl->Value.bin.cb = sizeof(DTBLLABEL) + strlen(szTranslated) + 1;

			lpTableData->HrModifyRow(lpsRow);

			delete[] lpLabel;
		} else if (lpPropType && lpPropControl && lpPropType->Value.ul == DTCT_GROUPBOX) {
			DTBLGROUPBOX *lpGroupBox = (DTBLGROUPBOX *)lpPropControl->Value.bin.lpb;

			// Found a label, translate the text through gettext
			char *szTranslated = _A((char *)lpGroupBox + lpGroupBox->ulbLpszLabel);

			// re-create a DTBLLABEL structure with the translated string
			lpGroupBox = (DTBLGROUPBOX *)new char [ sizeof(DTBLGROUPBOX) + strlen(szTranslated) + 1 ];
			lpGroupBox->ulbLpszLabel = sizeof(DTBLGROUPBOX);
			lpGroupBox->ulFlags = 0; // no unicode
			strcpy(((char *)lpGroupBox) + sizeof(DTBLGROUPBOX), szTranslated);

			lpPropControl->Value.bin.lpb = (LPBYTE)lpGroupBox;
			lpPropControl->Value.bin.cb = sizeof(DTBLGROUPBOX) + strlen(szTranslated) + 1;

			lpTableData->HrModifyRow(lpsRow);

			delete[] lpGroupBox;
		} else if(lpPropType && lpPropControl && lpPropType->Value.ul == DTCT_BUTTON) {
			DTBLBUTTON *lpButton = (DTBLBUTTON *)lpPropControl->Value.bin.lpb;

			// Found a label, translate the text through gettext
			char *szTranslated = _A((char *)lpButton + lpButton->ulbLpszLabel);

			// re-create a DTBLLABEL structure with the translated string
			lpButton = (DTBLBUTTON *)new char [ sizeof(DTBLBUTTON) + strlen(szTranslated) + 1 ];
			lpButton->ulbLpszLabel = sizeof(DTBLBUTTON);
			lpButton->ulFlags = 0; // no unicode
			strcpy(((char *)lpButton) + sizeof(DTBLBUTTON), szTranslated);

			lpPropControl->Value.bin.lpb = (LPBYTE)lpButton;
			lpPropControl->Value.bin.cb = sizeof(DTBLBUTTON) + strlen(szTranslated) + 1;

			lpTableData->HrModifyRow(lpsRow);

			delete[] lpButton;
		} else if(lpPropType && lpPropControl && lpPropType->Value.ul == DTCT_PAGE) {
			DTBLPAGE *lpPage = (DTBLPAGE *)lpPropControl->Value.bin.lpb;
			ULONG ulContext = lpPage->ulContext;
			char *szComponent = ((char *)lpPage) + lpPage->ulbLpszComponent;

			// Found a page, translate the label text through gettext
			char *szTranslated = _A((char *)lpPage + lpPage->ulbLpszLabel);

			// re-create a DTBLPAGE structure with the translated string and the original component name
			lpPage = (DTBLPAGE *)new char [ sizeof(DTBLPAGE) + strlen(szTranslated) + 1 + strlen(szComponent) + 1 ];
			lpPage->ulbLpszLabel = sizeof(DTBLPAGE);
			lpPage->ulbLpszComponent = sizeof(DTBLPAGE) + strlen(szTranslated) + 1;
			lpPage->ulContext = ulContext;
			lpPage->ulFlags = 0;

			strcpy(((char *)lpPage) + sizeof(DTBLPAGE), szTranslated);
			strcpy(((char *)lpPage) + sizeof(DTBLPAGE) + strlen(szTranslated) + 1, szComponent);

			lpPropControl->Value.bin.lpb = (LPBYTE)lpPage;
			lpPropControl->Value.bin.cb = sizeof(DTBLPAGE) + strlen(szTranslated) + 1 + strlen(szComponent) + 1;

			lpTableData->HrModifyRow(lpsRow);

			delete[] lpPage;
		} else if(lpPropType && lpPropControl && lpPropType->Value.ul == DTCT_EDIT) {
			DTBLEDIT *lpEdit = (DTBLEDIT *)lpPropControl->Value.bin.lpb;
			DTBLEDIT sEdit = *lpEdit;

			if(ulClientVersion <= CLIENT_VERSION_OLK2002) {
				// OLXP and earlier do not support PT_UNICODE in display tables
				if(PROP_TYPE(lpEdit->ulPropTag) == PT_UNICODE) {
					sEdit.ulPropTag = CHANGE_PROP_TYPE(sEdit.ulPropTag, PT_STRING8);

					lpPropControl->Value.bin.lpb = (LPBYTE)&sEdit;
				}
			}

			lpTableData->HrModifyRow(lpsRow);
		} else if(lpPropType && lpPropControl && lpPropType->Value.ul == DTCT_LBX) {
			DTBLLBX *lpLBX = (DTBLLBX *)lpPropControl->Value.bin.lpb;
			DTBLLBX sLBX = *lpLBX;
			if(ulClientVersion <= CLIENT_VERSION_OLK2002) {
				// OLXP and earlier do not support PT_UNICODE in display tables
				if(PROP_TYPE(sLBX.ulPRSetProperty) == PT_UNICODE) {
					sLBX.ulPRSetProperty = CHANGE_PROP_TYPE(sLBX.ulPRSetProperty, PT_STRING8);

					lpPropControl->Value.bin.lpb = (LPBYTE)&sLBX;
				}
			}

			lpTableData->HrModifyRow(lpsRow);
		} else if(lpPropType && lpPropControl && lpPropType->Value.ul == DTCT_DDLBX) {
			DTBLDDLBX *lpLBX = (DTBLDDLBX *)lpPropControl->Value.bin.lpb;
			DTBLDDLBX sLBX = *lpLBX;
			if(ulClientVersion <= CLIENT_VERSION_OLK2002) {
				// OLXP and earlier do not support PT_UNICODE in display tables
				if(PROP_TYPE(sLBX.ulPRDisplayProperty) == PT_UNICODE) {
					sLBX.ulPRDisplayProperty = CHANGE_PROP_TYPE(sLBX.ulPRDisplayProperty, PT_STRING8);
					lpPropControl->Value.bin.lpb = (LPBYTE)&sLBX;
				}

				if(PROP_TYPE(sLBX.ulPRSetProperty) == PT_UNICODE) {
					sLBX.ulPRSetProperty = CHANGE_PROP_TYPE(sLBX.ulPRSetProperty, PT_STRING8);
					lpPropControl->Value.bin.lpb = (LPBYTE)&sLBX;
				}
			}
			lpTableData->HrModifyRow(lpsRow);
		} 
		else {
			// Update the row anyway, so the row order is kept in the table
			lpTableData->HrModifyRow(lpsRow);
		}

		MAPIFreeBuffer(lpsRow);
		lpsRow = NULL;
	}
	MAPIFreeBuffer(lpsRow);
}

HRESULT ECDisplayTable::CreateDisplayTable(ULONG ulPages, DTPAGE *lpPages, IMAPITable **lppTable)
{
	HRESULT		hr = hrSuccess;
	ITableData	*lpTableData = NULL;

	hr = BuildDisplayTable(
						   GetAllocateBuffer(),
						   GetAllocateMore(),
						   GetFreeBuffer(),
						   GetMalloc(),
						   GetInstance(),//hLibrary
						   ulPages,
						   lpPages,
						   NULL,
						   (LPMAPITABLE *) lppTable,
						   &lpTableData);
	if (hr != hrSuccess)
		goto exit;

	TranslateDisplayTable(lpTableData);

exit:
	if (lpTableData)
		lpTableData->Release();

	return hr;
}

HRESULT ECDisplayTable::CreateTableEmpty(ECABProp *lpABProp, LPCIID lpiid, ULONG ulInterfaceOptions, LPUNKNOWN *lppUnk)
{
	HRESULT			hr = hrSuccess;
	ECMemTable		*lpTable = NULL;
	ECMemTableView	*lpView = NULL;

	SizedSPropTagArray(1, tagaColSet) = {
		1, {
			PR_ROWID,
	   }
	};

	if ((ulInterfaceOptions & ~MAPI_UNICODE) != 0) {
		hr = MAPI_E_UNKNOWN_FLAGS;
		goto exit;
	}

	hr = ECMemTable::Create((LPSPropTagArray)&tagaColSet, PR_ROWID, &lpTable);
	if (hr != hrSuccess)
		goto exit;

	hr = lpTable->HrClear();
	if (hr != hrSuccess)
		goto exit;

	hr = lpTable->HrSetClean();
	if (hr != hrSuccess)
		goto exit;

	hr = lpTable->HrGetView(createLocaleFromName(""), ulInterfaceOptions, &lpView);
	if(hr != hrSuccess)
		goto exit;

	if (lppUnk) {
		hr = lpView->QueryInterface(*lpiid, (LPVOID *)(LPMAPITABLE *)lppUnk);
		if (hr != hrSuccess)
			goto exit;
	}

exit:
	if (lpView)
		lpView->Release();
	if (lpTable)
		lpTable->Release();

	return hr;
}

HRESULT ECDisplayTable::CreateTableFromProperty(ECABProp *lpABProp, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulColumnTag, ULONG ulPropTag, LPUNKNOWN *lppUnk)
{
	HRESULT			hr = hrSuccess;
	ECMemTable		*lpTable = NULL;
	ECMemTableView	*lpView = NULL;
	LPSPropValue	lpProps = NULL;
	ULONG			ulProps = 0;
	ULONG			ulRowId = 0;
	ULONG			ulMVPropTag = 0;

	SPropValue		sEntry[2];
	LPSPropValue	lpValue = NULL;		// non-free


	if(ulInterfaceOptions&MAPI_UNICODE){
		if(PROP_TYPE(ulColumnTag) == PT_STRING8)			ulColumnTag = CHANGE_PROP_TYPE(ulColumnTag, PT_UNICODE);
		else if(PROP_TYPE(ulColumnTag) == PT_MV_STRING8)	ulColumnTag = CHANGE_PROP_TYPE(ulColumnTag, PT_MV_UNICODE);

		if(PROP_TYPE(ulPropTag) == PT_STRING8)				ulPropTag = CHANGE_PROP_TYPE(ulPropTag, PT_UNICODE);
		else if(PROP_TYPE(ulPropTag) == PT_MV_STRING8)		ulPropTag = CHANGE_PROP_TYPE(ulPropTag, PT_MV_UNICODE);
	} else {
		if(PROP_TYPE(ulColumnTag) == PT_UNICODE)			ulColumnTag = CHANGE_PROP_TYPE(ulColumnTag, PT_STRING8);
		else if(PROP_TYPE(ulColumnTag) == PT_MV_UNICODE)	ulColumnTag = CHANGE_PROP_TYPE(ulColumnTag, PT_MV_STRING8);

		if(PROP_TYPE(ulPropTag) == PT_UNICODE)				ulPropTag = CHANGE_PROP_TYPE(ulPropTag, PT_STRING8);
		else if(PROP_TYPE(ulPropTag) == PT_MV_UNICODE)		ulPropTag = CHANGE_PROP_TYPE(ulPropTag, PT_MV_STRING8);
	}

	ulMVPropTag = CHANGE_PROP_TYPE(ulPropTag, PROP_TYPE(ulPropTag) | MV_FLAG);

	SizedSPropTagArray(2, tagaColumns) = {
		2, {
			ulColumnTag,
			PR_ROWID, /* KEEP LAST */
	   }
	};

	SizedSPropTagArray(2, tagaPropGet) = {
		2, {
			ulPropTag,
			/*
			 * It is poorly documented, but Exchange provides
			 * some proptags (like PR_BUSINESS2_TELEPHONE_NUMBER)
			 * as multi-value properties as well.
			 */
			ulMVPropTag,
		}
	};

	if ((ulInterfaceOptions & ~MAPI_UNICODE) != 0) {
		hr = MAPI_E_UNKNOWN_FLAGS;
		goto exit;
	}

	/* In case ulPropTag already is a MV property */
	if (tagaPropGet.aulPropTag[0] == tagaPropGet.aulPropTag[1])
		tagaPropGet.cValues = 1;

	SSortOrderSet sSort;
	sSort.cSorts = 1;
	sSort.cCategories = 0;
	sSort.cExpanded = 0;
	sSort.aSort[0].ulPropTag = ulColumnTag;
	sSort.aSort[0].ulOrder = TABLE_SORT_ASCEND;

	hr = ECMemTable::Create((LPSPropTagArray)&tagaColumns, PR_ROWID, &lpTable);
	if (hr != hrSuccess)
		goto exit;

	hr = lpTable->HrClear();
	if (hr != hrSuccess)
		goto exit;

	hr = lpABProp->GetProps((LPSPropTagArray)&tagaPropGet, 0, &ulProps, &lpProps);
	if (FAILED(hr)) {
		/* Really, we don't care. Just return an empty table */
		ulProps = 0;
		lpProps = NULL;
	}

	/* Overwrite PR_ROWID */
	sEntry[0].ulPropTag = PR_ROWID;
	sEntry[0].Value.ul = 1;

	if (!(ulPropTag & MV_FLAG)) {
		lpValue = PpropFindProp(lpProps, ulProps, ulPropTag);
		if (lpValue) {
			sEntry[1].ulPropTag = ulColumnTag;
			sEntry[1].Value.lpszA = lpValue->Value.lpszA;

			hr = lpTable->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, NULL, sEntry, arraySize(sEntry));
			if (hr != hrSuccess)
				goto exit;

			/* Increase PR_ROWID */
			++sEntry[0].Value.ul;
		}
	}

	lpValue = PpropFindProp(lpProps, ulProps, ulMVPropTag);
	if (lpValue) {
		for (unsigned int j = 0; j < lpValue->Value.MVszA.cValues; ++j) {
			sEntry[1].ulPropTag = ulColumnTag;
			sEntry[1].Value.lpszA = lpValue->Value.MVszA.lppszA[j];

			hr = lpTable->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, NULL, sEntry, arraySize(sEntry));
			if (hr != hrSuccess)
				goto exit;

			/* Increase PR_ROWID */
			++sEntry[0].Value.ul;
		}
	}

	hr = lpTable->HrSetClean();
	if (hr != hrSuccess)
		goto exit;

	hr = lpTable->HrGetView(createLocaleFromName(""), ulInterfaceOptions, &lpView);
	if(hr != hrSuccess)
		goto exit;

	/* Remote PR_ROWID, and set columns */
	--tagaColumns.cValues;
	hr = lpView->SetColumns((LPSPropTagArray)&tagaColumns, MAPI_DEFERRED_ERRORS);
	if (hr != hrSuccess)
		goto exit;

	hr = lpView->SortTable(&sSort, MAPI_DEFERRED_ERRORS);
	if(hr != hrSuccess)
		goto exit;

	if (lppUnk) {
		hr = lpView->QueryInterface(*lpiid, (LPVOID *)(LPMAPITABLE *)lppUnk);
		if (hr != hrSuccess)
			goto exit;
	}

exit:
	MAPIFreeBuffer(lpProps);
	if (lpView)
		lpView->Release();
	if (lpTable)
		lpTable->Release();

	return hr;
}

HRESULT ECDisplayTable::CreateTableFromResolved(ECABProp *lpABProp, LPCIID lpiid, ULONG ulInterfaceOptions, ULONG ulPropTag, LPUNKNOWN *lppUnk)
{
	HRESULT			hr = hrSuccess;
	ECMemTable		*lpTable = NULL;
	ECMemTableView	*lpView = NULL;
	LPADRLIST		lpAddrList = NULL;
	ULONG			ulRowId = 0;

	SizedSPropTagArray(7, tagaColumns) = {
		7, {
			PROP_TAG(((ulInterfaceOptions & MAPI_UNICODE)?PT_UNICODE:PT_STRING8), PROP_ID(PR_DISPLAY_NAME)),
			PR_ENTRYID,
			PR_INSTANCE_KEY,
			PR_DISPLAY_TYPE,
			PROP_TAG(((ulInterfaceOptions & MAPI_UNICODE)?PT_UNICODE:PT_STRING8), PROP_ID(PR_SMTP_ADDRESS)),
			PROP_TAG(((ulInterfaceOptions & MAPI_UNICODE)?PT_UNICODE:PT_STRING8), PROP_ID(PR_ACCOUNT)),
			PR_ROWID, /* KEEP LAST */
		}
	};

	if ((ulInterfaceOptions & ~MAPI_UNICODE) != 0) {
		hr = MAPI_E_UNKNOWN_FLAGS;
		goto exit;
	}

	if(ulInterfaceOptions & MAPI_UNICODE) {
		if(PROP_TYPE(ulPropTag) == PT_STRING8)				ulPropTag = CHANGE_PROP_TYPE(ulPropTag, PT_UNICODE);
		else if(PROP_TYPE(ulPropTag) == PT_MV_STRING8)		ulPropTag = CHANGE_PROP_TYPE(ulPropTag, PT_MV_UNICODE);
	} else {
		if(PROP_TYPE(ulPropTag) == PT_UNICODE)				ulPropTag = CHANGE_PROP_TYPE(ulPropTag, PT_STRING8);
		else if(PROP_TYPE(ulPropTag) == PT_MV_UNICODE)		ulPropTag = CHANGE_PROP_TYPE(ulPropTag, PT_MV_STRING8);
	}

	hr = ECMemTable::Create((LPSPropTagArray)&tagaColumns, PR_ROWID, &lpTable);
	if(hr != hrSuccess)
		goto exit;

	hr = ResolveFromProperty(lpABProp, ulPropTag, (LPSPropTagArray)&tagaColumns, &lpAddrList);
	if (FAILED(hr)) {
		/* Don't care, just create an empty table */
		lpAddrList = NULL;
	}

	if (lpAddrList) {
		for (ULONG i = 0; i < lpAddrList->cEntries; ++i) {
			LPSPropValue lpProps = lpAddrList->aEntries[i].rgPropVals;
			ULONG ulProps = lpAddrList->aEntries[i].cValues;

			/* Overwrite PR_ROWID */
			for (ULONG j = 0; j < ulProps; ++j) {
				if (PROP_ID(lpProps[j].ulPropTag) != PROP_ID(PR_ROWID))
					continue;

				lpProps[j].ulPropTag = PR_ROWID;
				lpProps[j].Value.ul = i;
			}

			hr = lpTable->HrModifyRow(ECKeyTable::TABLE_ROW_ADD, NULL, lpProps, ulProps);
			if (hr != hrSuccess)
				goto exit;
		}
	}

	hr = lpTable->HrSetClean();
	if (hr != hrSuccess)
		goto exit;

	hr = lpTable->HrGetView(createLocaleFromName(""), ulInterfaceOptions, &lpView);
	if(hr != hrSuccess)
		goto exit;

	/* Remote PR_ROWID, and set columns */
	--tagaColumns.cValues;
	hr = lpView->SetColumns((LPSPropTagArray)&tagaColumns, MAPI_DEFERRED_ERRORS);
	if (hr != hrSuccess)
		goto exit;

	if (lppUnk) {
		hr = lpView->QueryInterface(*lpiid, (LPVOID *)(LPMAPITABLE *)lppUnk);
		if (hr != hrSuccess)
			goto exit;
	}

exit:
	if (lpAddrList)
		FreePadrlist(lpAddrList);
	if (lpView)
		lpView->Release();
	if (lpTable)
		lpTable->Release();

	return hr;
}

HRESULT ECDisplayTable::ResolveFromEntryId(IABContainer *lpABContainer, LPSPropValue lpEntryId,
										   LPSPropTagArray lpGetProps, LPADRLIST *lppAddrList)
{
	HRESULT			hr = hrSuccess;
	LPADRLIST		lpAddrList = NULL;
	IMAPIProp		*lpABObject = NULL;
	ULONG			ulObj = 0;

	if (lpEntryId->ulPropTag & MV_FLAG) {
		hr = MAPIAllocateBuffer(CbNewADRLIST(lpEntryId->Value.MVbin.cValues), (LPVOID *)&lpAddrList);
		if (hr != hrSuccess)
			goto exit;

		lpAddrList->cEntries = 0;

		for (ULONG i = 0; i < lpEntryId->Value.MVbin.cValues; ++i) {
			hr = lpABContainer->OpenEntry(lpEntryId->Value.MVbin.lpbin[i].cb, (LPENTRYID)lpEntryId->Value.MVbin.lpbin[i].lpb,
										  &IID_IMAPIProp, 0, &ulObj, (LPUNKNOWN *)&lpABObject);
			if (hr != hrSuccess) {
				if (hr == MAPI_E_NOT_FOUND) {
					hr = hrSuccess;
					continue;
				}
				goto exit;
			}

			hr = lpABObject->GetProps(lpGetProps, 0, &lpAddrList->aEntries[lpAddrList->cEntries].cValues, &lpAddrList->aEntries[lpAddrList->cEntries].rgPropVals);
			if (FAILED(hr))
				goto exit;

			hr = hrSuccess;
			++lpAddrList->cEntries;
		}
	} else {
		hr = MAPIAllocateBuffer(CbNewADRLIST(1), (LPVOID *)&lpAddrList);
		if (hr != hrSuccess)
			goto exit;

		hr = lpABContainer->OpenEntry(lpEntryId->Value.bin.cb, (LPENTRYID)lpEntryId->Value.bin.lpb,
									  &IID_IMAPIProp, 0, &ulObj, (LPUNKNOWN *)&lpABObject);
		if (hr != hrSuccess)
			goto exit;

		hr = lpABObject->GetProps(lpGetProps, 0, &lpAddrList->aEntries[0].cValues, &lpAddrList->aEntries[0].rgPropVals);
		if (FAILED(hr))
			goto exit;

		hr = hrSuccess;
		lpAddrList->cEntries = 1;
	}

	/* warnings are not fatal */
	hr = hrSuccess;

	if (lppAddrList) {
		*lppAddrList = lpAddrList;
		lpAddrList = NULL;
	}

exit:
	if (lpAddrList)
		FreePadrlist(lpAddrList);
	if (lpABObject)
		lpABObject->Release();

	return hr;
}

HRESULT ECDisplayTable::ResolveFromName(IABContainer *lpABContainer, LPSPropValue lpName,
										LPSPropTagArray lpGetProps, LPADRLIST *lppAddrList)
{
	HRESULT			hr = hrSuccess;
	LPADRLIST		lpResolve = NULL;
	LPFlagList		lpResolveFlags = NULL;

	if (lpName == NULL || (lpName->ulPropTag & MV_FLAG)) {
		hr = MAPI_E_INVALID_PARAMETER;
		goto exit;
	}

	hr = MAPIAllocateBuffer(CbNewADRLIST(1), (LPVOID *)&lpResolve);
	if (hr != hrSuccess)
		goto exit;

	hr = MAPIAllocateBuffer(sizeof(*lpResolveFlags), (LPVOID *)&lpResolveFlags);
	if (hr != hrSuccess)
		goto exit;

	lpResolve->cEntries = 1;
	lpResolve->aEntries[0].cValues = 1; // properties

	lpResolveFlags->cFlags = 1;
	lpResolveFlags->ulFlag[0] = MAPI_UNRESOLVED;

	hr = MAPIAllocateBuffer(sizeof(SPropValue) * lpResolve->aEntries[0].cValues, (LPVOID *)&lpResolve->aEntries[0].rgPropVals);
	if (hr != hrSuccess)
		goto exit;

	lpResolve->aEntries[0].rgPropVals[0].ulPropTag = PR_DISPLAY_NAME;
	lpResolve->aEntries[0].rgPropVals[0].Value.lpszA = lpName->Value.lpszA;

	hr = lpABContainer->ResolveNames(lpGetProps, 0, lpResolve, lpResolveFlags);
	if (hr != hrSuccess)
		goto exit;

	if (lpResolveFlags->ulFlag[0] != MAPI_RESOLVED) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	if (lppAddrList) {
		*lppAddrList = lpResolve;
		lpResolve = NULL;
	}

exit:
	if (lpResolve)
		FreePadrlist(lpResolve);
	MAPIFreeBuffer(lpResolveFlags);
	return hr;
}

HRESULT ECDisplayTable::ResolveFromProperty(ECABProp *lpABProp, ULONG ulPropTag, LPSPropTagArray lpGetProps, LPADRLIST *lppAddrList)
{
	HRESULT			hr = hrSuccess;
	IABContainer	*lpABContainer = NULL;
	IMAPIProp		*lpABObject = NULL;
	LPSPropValue	lpProps = NULL;
	ULONG			ulBinaryTag = CHANGE_PROP_TYPE(ulPropTag, (PROP_TYPE(ulPropTag) & MV_FLAG) | PT_BINARY);
	ULONG			ulProps = 0;
	ULONG			ulObj = 0;
	LPSPropValue	lpNameProp = NULL;		// non-free
	LPSPropValue	lpBinaryProp = NULL;	// non-free

	SizedSPropTagArray(2, tagaPropGet) = {
		2, {
			ulPropTag,
			ulBinaryTag,
		}
	};

	hr = lpABProp->GetProps((LPSPropTagArray)&tagaPropGet, 0, &ulProps, &lpProps);
	if (FAILED(hr))
		goto exit;

	lpBinaryProp = PpropFindProp(lpProps, ulProps, ulBinaryTag);
	lpNameProp = PpropFindProp(lpProps, ulProps, ulPropTag);
	if (!lpNameProp && !lpBinaryProp) {
		hr = MAPI_E_NOT_FOUND;
		goto exit;
	}

	hr = lpABProp->GetABStore()->OpenEntry(0, NULL, &IID_IABContainer, 0, &ulObj, (LPUNKNOWN *)&lpABContainer);
	if (hr != hrSuccess)
		goto exit;

	if (lpBinaryProp) {
		hr = ResolveFromEntryId(lpABContainer, lpBinaryProp, lpGetProps, lppAddrList);
		if (hr != hrSuccess)
			goto exit;
	} else {
		hr = ResolveFromName(lpABContainer, lpNameProp, lpGetProps, lppAddrList);
		if (hr != hrSuccess)
			goto exit;
	}

exit:
	MAPIFreeBuffer(lpProps);
	if (lpABContainer)
		lpABContainer->Release();

	return hr;
}
#endif
