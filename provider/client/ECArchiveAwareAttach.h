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

#ifndef ECARCHIVEAWAREATTACH_INCLUDED
#define ECARCHIVEAWAREATTACH_INCLUDED

#include <kopano/zcdefs.h>
#include "ECAttach.h"

class ECArchiveAwareMessage;

class ECArchiveAwareAttach _kc_final : public ECAttach {
protected:
	ECArchiveAwareAttach(ECMsgStore *lpMsgStore, ULONG ulObjType, BOOL fModify, ULONG ulAttachNum, ECMAPIProp *lpRoot);

public:
	static	HRESULT Create(ECMsgStore *lpMsgStore, ULONG ulObjType, BOOL fModify, ULONG ulAttachNum, ECMAPIProp *lpRoot, ECAttach **lppAttach);
	static HRESULT SetPropHandler(ULONG ulPropTag, void *lpProvider, const SPropValue *lpsPropValue, void *lpParam);

private:
	ECArchiveAwareMessage	*m_lpRoot;
};

class ECArchiveAwareAttachFactory _kc_final : public IAttachFactory {
public:
	HRESULT Create(ECMsgStore *lpMsgStore, ULONG ulObjType, BOOL fModify, ULONG ulAttachNum, ECMAPIProp *lpRoot, ECAttach **lppAttach) const;
};


#endif // ndef ECARCHIVEAWAREATTACH_INCLUDED
