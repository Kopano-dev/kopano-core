virtual HRESULT __stdcall GetContentsTable(ULONG flags, LPMAPITABLE *lppTable) _kc_override;
virtual HRESULT __stdcall GetHierarchyTable(ULONG flags, LPMAPITABLE *lppTable) _kc_override;
virtual HRESULT __stdcall OpenEntry(ULONG cbEntryID, LPENTRYID lpEntryID, LPCIID lpInterface, ULONG flags, ULONG *lpulObjType, LPUNKNOWN *lppUnk) _kc_override;
virtual HRESULT __stdcall SetSearchCriteria(LPSRestriction lpRestriction, LPENTRYLIST lpContainerList, ULONG ulSearchFlags) _kc_override;
virtual HRESULT __stdcall GetSearchCriteria(ULONG flags, LPSRestriction *lppRestriction, LPENTRYLIST *lppContainerList, ULONG *lpulSearchState) _kc_override;
