/*
 * SPDX-License-Identifier: AGPL-3.0-only
 * Copyright 2005 - 2016 Zarafa and its licensors
 */

#ifndef ECMEMSTREAM_H
#define ECMEMSTREAM_H

#include <kopano/zcdefs.h>
#include <kopano/ECUnknown.h>
#include <kopano/Util.h>

namespace KC {

/* The ECMemBlock class is basically a random-access block of data that can be
 * read from and written to, expanded and contracted, and has a Commit and Revert
 * function to save and reload data.
 *
 * The commit and revert functions use memory sparingly, as only changed blocks
 * are held in memory.
 */
class ECMemBlock KC_FINAL_OPG : public ECUnknown {
private:
	ECMemBlock(const char *buffer, ULONG len, ULONG flags);
	~ECMemBlock();

public:
	static HRESULT Create(const char *buffer, ULONG len, ULONG flags, ECMemBlock **out);
	virtual HRESULT QueryInterface(const IID &, void **) override;
	virtual HRESULT	ReadAt(unsigned int pos, unsigned int len, char *buffer, unsigned int *have_read);
	virtual HRESULT WriteAt(unsigned int pos, unsigned int len, const char *buffer, unsigned int *have_written);
	virtual HRESULT Commit();
	virtual HRESULT Revert();
	virtual HRESULT SetSize(ULONG ulSize);
	virtual HRESULT GetSize(ULONG *size) const;
	virtual char *GetBuffer(void) { return lpCurrent; }

private:
	char *lpCurrent = nullptr, *lpOriginal = nullptr;
	ULONG cbCurrent = 0, cbOriginal = 0, cbTotal = 0;
	ULONG	ulFlags;
	ALLOC_WRAP_FRIEND;
};

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
	static  HRESULT	Create(char *buffer, ULONG ulDataLen, ULONG ulFlags, CommitFunc lpCommitFunc, DeleteFunc lpDeleteFunc, void *lpParam, ECMemStream **lppStream);
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
	ULARGE_INTEGER liPos = {{0}};
	ECMemBlock *lpMemBlock = nullptr;
	CommitFunc		lpCommitFunc;
	DeleteFunc		lpDeleteFunc;
	void *			lpParam;
	bool fDirty = false;
	ULONG			ulFlags;
	ALLOC_WRAP_FRIEND;
};

} /* namespace */

#endif // ECMEMSTREAM_H
