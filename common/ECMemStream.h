/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */
#pragma once
#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>
#include <kopano/Util.h>

namespace KC {

class ECMemBlock;

/*
 * This is an IStream-compatible wrapper for ECMemBlock
 */
class KC_EXPORT ECMemStream KC_FINAL_OPG : public ECUnknown, public IStream {
public:
	typedef HRESULT (*CommitFunc)(IStream *lpStream, void *lpParam);
	typedef HRESULT (*DeleteFunc)(void *lpParam); /* Caller's function to remove lpParam data from memory */

private:
	KC_HIDDEN ECMemStream(const char *buffer, unsigned int data_len, unsigned int flags, CommitFunc, DeleteFunc, void *param);
	KC_HIDDEN ECMemStream(ECMemBlock *, unsigned int flags, CommitFunc, DeleteFunc, void *param);

	protected:
	KC_HIDDEN ~ECMemStream();

public:
	static HRESULT Create(const char *buffer, unsigned int len, unsigned int flags, CommitFunc, DeleteFunc, void *param, ECMemStream **);
	KC_HIDDEN static HRESULT Create(ECMemBlock *, unsigned int flags, CommitFunc, DeleteFunc, void *param, ECMemStream **ret);
	virtual unsigned int Release() override;
	virtual HRESULT QueryInterface(const IID &, void **) override;
	KC_HIDDEN virtual HRESULT Read(void *buf, unsigned int bytes, unsigned int *have_read) override;
	KC_HIDDEN virtual HRESULT Write(const void *buf, unsigned int bytes, unsigned int *have_written) override;
	KC_HIDDEN virtual HRESULT Seek(LARGE_INTEGER pos, unsigned int origin, ULARGE_INTEGER *newpos) override;
	KC_HIDDEN virtual HRESULT SetSize(ULARGE_INTEGER size) override;
	KC_HIDDEN virtual HRESULT CopyTo(IStream *, ULARGE_INTEGER cb, ULARGE_INTEGER *have_read, ULARGE_INTEGER *have_written) override;
	KC_HIDDEN virtual HRESULT Commit(unsigned int commit_flags) override;
	KC_HIDDEN virtual HRESULT Revert() override;
	KC_HIDDEN virtual HRESULT LockRegion(ULARGE_INTEGER offset, ULARGE_INTEGER size, unsigned int lock_type) override;
	KC_HIDDEN virtual HRESULT UnlockRegion(ULARGE_INTEGER offset, ULARGE_INTEGER size, unsigned int lock_type) override;
	KC_HIDDEN virtual HRESULT Stat(STATSTG *, unsigned int stat_flag) override;
	KC_HIDDEN virtual HRESULT Clone(IStream **) override;
	virtual ULONG GetSize();
	virtual char* GetBuffer();

private:
	ULARGE_INTEGER liPos{};
	ECMemBlock *lpMemBlock = nullptr;
	CommitFunc		lpCommitFunc;
	DeleteFunc		lpDeleteFunc;
	void *			lpParam;
	bool fDirty = false;
	ULONG			ulFlags;
	ALLOC_WRAP_FRIEND;
};

} /* namespace */
