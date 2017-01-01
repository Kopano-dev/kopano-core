virtual HRESULT __stdcall CreateEntry(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulCreateFlags, LPMAPIPROP *lppMAPIPropEntry) _kc_override;
virtual HRESULT __stdcall CopyEntries(LPENTRYLIST lpEntries, ULONG ui_param, LPMAPIPROGRESS lpProgress, ULONG flags) _kc_override;
virtual HRESULT __stdcall DeleteEntries(LPENTRYLIST lpEntries, ULONG flags) _kc_override;
virtual HRESULT __stdcall ResolveNames(const SPropTagArray *lpPropTagArray, ULONG flags, LPADRLIST lpAdrList, LPFlagList lpFlagList) _kc_override;
