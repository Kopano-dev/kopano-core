virtual HRESULT __stdcall Seek(LARGE_INTEGER dlibmove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition) _kc_override;
virtual HRESULT __stdcall SetSize(ULARGE_INTEGER libNewSize) _kc_override;
virtual HRESULT __stdcall CopyTo(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten) _kc_override;
virtual HRESULT __stdcall Commit(DWORD grfCommitFlags) _kc_override;
virtual HRESULT __stdcall Revert(void) _kc_override;
virtual HRESULT __stdcall LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) _kc_override;
virtual HRESULT __stdcall UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) _kc_override;
virtual HRESULT __stdcall Stat(STATSTG *pstatstg, DWORD grfStatFlag) _kc_override;
virtual HRESULT __stdcall Clone(IStream **ppstm) _kc_override;
