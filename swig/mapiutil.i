%apply (ULONG cbEntryID, LPENTRYID lpEntryID) {(ULONG cbOrigEntry, LPENTRYID lpOrigEntry)};

HRESULT WrapStoreEntryID(ULONG ulFlags, LPTSTR lpszDLLName, ULONG cbOrigEntry, LPENTRYID lpOrigEntry, ULONG *OUTPUT, LPENTRYID *OUTPUT);
// HRESULT UnWrapStoreEntryID(ULONG cbOrigEntry, LPENTRYID lpOrigEntry, ULONG *OUTPUT, LPENTRYID *OUTPUT);

HRESULT WrapCompressedRTFStream(IStream *lpCompressedRTFStream, ULONG ulFlags, IStream ** lppUncompressedStream);