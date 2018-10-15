/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef stubber_INCLUDED
#define stubber_INCLUDED

#include <memory>
#include "operations.h"

namespace KC { namespace operations {

/**
 * Performs the stub part of the archive oepration.
 */
class Stubber final : public ArchiveOperationBase {
public:
	Stubber(std::shared_ptr<ECArchiverLogger>, ULONG ulptStubbed, int ulAge, bool bProcessUnread);
	HRESULT ProcessEntry(IMAPIFolder *, const SRow &proprow) override;
	HRESULT ProcessEntry(LPMESSAGE lpMessage);

private:
	ULONG m_ulptStubbed;
};

}} /* namespace */

#endif // ndef stubber_INCLUDED
