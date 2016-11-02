virtual HRESULT __stdcall CreateStoreEntryID(LPTSTR lpszMsgStoreDN, LPTSTR lpszMailboxDN, ULONG flags, ULONG *lpcbEntryID, LPENTRYID *lppEntryID) _kc_override;
virtual HRESULT __stdcall EntryIDFromSourceKey(ULONG cFolderKeySize, BYTE *lpFolderSourceKey, ULONG cMessageKeySize, BYTE *lpMessageSourceKey, ULONG *lpcbEntryID, LPENTRYID *lppEntryID) _kc_override;
virtual HRESULT __stdcall GetRights(ULONG cbUserEntryID, LPENTRYID lpUserEntryID, ULONG cbEntryID, LPENTRYID lpEntryID, ULONG *lpulRights) _kc_override;
virtual HRESULT __stdcall GetMailboxTable(LPTSTR lpszServerName, LPMAPITABLE *lppTable, ULONG flags) _kc_override;
virtual HRESULT __stdcall GetPublicFolderTable(LPTSTR lpszServerName, LPMAPITABLE *lppTable, ULONG flags) _kc_override;
