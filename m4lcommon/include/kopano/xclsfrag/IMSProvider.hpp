virtual HRESULT __stdcall Shutdown(ULONG *lpulFlags) _kc_override;
virtual HRESULT __stdcall Logon(LPMAPISUP lpMAPISup, ULONG_PTR ui_param, LPTSTR profname, ULONG eid_size, LPENTRYID eid, ULONG flags, LPCIID intf, ULONG *spoolsec_size, LPBYTE *spoolsec, LPMAPIERROR *, LPMSLOGON *, LPMDB *) _kc_override;
virtual HRESULT __stdcall SpoolerLogon(LPMAPISUP lpMAPISup, ULONG_PTR ui_param, LPTSTR profname, ULONG eid_size, LPENTRYID eid, ULONG flags, LPCIID intf, ULONG spoolsec_size, LPBYTE spoolsec, LPMAPIERROR *, LPMSLOGON *, LPMDB *) _kc_override;
virtual HRESULT __stdcall CompareStoreIDs(ULONG cbEntryID1, LPENTRYID lpEntryID1, ULONG cbEntryID2, LPENTRYID lpEntryID2, ULONG flags, ULONG *lpulResult) _kc_override;
