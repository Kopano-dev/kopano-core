/* SPDX-License-Identifier: AGPL-3.0-only */
%apply (ULONG cbEntryID, LPENTRYID lpEntryID) {(ULONG cbOrigEntry, LPENTRYID lpOrigEntry)};

HRESULT WrapStoreEntryID(ULONG ulFlags, LPTSTR lpszDLLName, ULONG cbOrigEntry, LPENTRYID lpOrigEntry, ULONG *OUTPUT, LPENTRYID *OUTPUT);

HRESULT WrapCompressedRTFStream(IStream *lpCompressedRTFStream, ULONG ulFlags, IStream ** lppUncompressedStream);

%typemap(in,numinputs=0)    const ECLocale & (ECLocale bert)
{
    bert = createLocaleFromName("");
    $1 = &bert;
}

HRESULT TestRestriction(LPSRestriction lpRestriction, IMessage *lpMessage, const ECLocale &);
