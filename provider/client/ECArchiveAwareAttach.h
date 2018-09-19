/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#ifndef ECARCHIVEAWAREATTACH_INCLUDED
#define ECARCHIVEAWAREATTACH_INCLUDED

#include <kopano/zcdefs.h>
#include <kopano/Util.h>
#include "ECAttach.h"

class ECArchiveAwareMessage;

class ECArchiveAwareAttach final : public ECAttach {
protected:
	ECArchiveAwareAttach(ECMsgStore *, ULONG obj_type, BOOL modify, ULONG attach_num, const ECMAPIProp *root);

public:
	static HRESULT Create(ECMsgStore *, ULONG obj_type, BOOL modify, ULONG attach_num, const ECMAPIProp *root, ECAttach **);
	static HRESULT SetPropHandler(ULONG ulPropTag, void *lpProvider, const SPropValue *lpsPropValue, void *lpParam);

private:
	const ECArchiveAwareMessage *m_lpRoot;
	ALLOC_WRAP_FRIEND;
};

class ECArchiveAwareAttachFactory final : public IAttachFactory {
public:
	HRESULT Create(ECMsgStore *, ULONG obj_type, BOOL modify, ULONG attach_num, const ECMAPIProp *root, ECAttach **) const;
};

#endif // ndef ECARCHIVEAWAREATTACH_INCLUDED
