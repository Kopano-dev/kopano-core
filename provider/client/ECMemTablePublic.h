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

#ifndef ECMEMTABLEPUBLIC_H
#define ECMEMTABLEPUBLIC_H

#include <kopano/zcdefs.h>
#include <kopano/ECMemTable.h>
#include "ECMAPIFolderPublic.h"

#include <mapidefs.h>

class ECMemTablePublic _kc_final : public ECMemTable {
public:
	struct t_sRelation {
		unsigned int ulRowID;
		IMAPIFolder* lpFolder;
		LPMAPIADVISESINK lpAdviseSink;
		ULONG ulAdviseConnectionId;
		ULONG cbEntryID; // Folder entryid
		LPENTRYID lpEntryID;
	};

	typedef std::map<std::string, t_sRelation> ECMAPFolderRelation; // <instancekey, relation>

protected:
	ECMemTablePublic(ECMAPIFolderPublic *lpECParentFolder, SPropTagArray *lpsPropTags, ULONG ulRowPropTag);
	virtual ~ECMemTablePublic(void);

public:
	static HRESULT Create(ECMAPIFolderPublic *lpECParentFolder, ECMemTablePublic **lppECMemTable);
	
	static void FreeRelation(t_sRelation* lpRelation);
	HRESULT QueryInterface(REFIID refiid, void **lppInterface) _kc_override;
	//virtual ULONG AddRef(void) _kc_override;
	//virtual ULONG Release(void) _kc_override;
	HRESULT Init(ULONG ulFlags);

	HRESULT ModifyRow(SBinary* lpInstanceKey, LPSRow lpsRow);
	HRESULT DelRow(SBinary* lpInstanceKey);


	HRESULT AdviseFolder(ULONG cbSourceKey, LPBYTE lpbSourceKey, LPMAPIFOLDER lpFolder);

	ECMAPIFolderPublic *m_lpECParentFolder;
	LPMAPIADVISESINK	m_lpShortCutAdviseSink;
	ULONG				m_ulFlags; //UNICODE flags
	LPMAPITABLE			m_lpShortcutTable;

	ULONG				m_ulRowId;
	ECMAPFolderRelation	m_mapRelation; //Relation between shortcut instancekey and rowid

};

#endif //#ifndef ECMEMTABLEPUBLIC_H
