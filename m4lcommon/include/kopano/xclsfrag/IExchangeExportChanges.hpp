virtual HRESULT __stdcall GetLastError(HRESULT hResult, ULONG flags, LPMAPIERROR *lppMAPIError) _kc_override;
virtual HRESULT __stdcall Config(LPSTREAM lpStream, ULONG flags, LPUNKNOWN lpCollector, LPSRestriction lpRestriction, LPSPropTagArray inclprop, LPSPropTagArray exclprop, ULONG ulBufferSize) _kc_override;
virtual HRESULT __stdcall Synchronize(ULONG *pulSteps, ULONG *pulProgress) _kc_override;
virtual HRESULT __stdcall UpdateState(LPSTREAM lpStream) _kc_override;

virtual HRESULT __stdcall ConfigSelective(ULONG ulPropTag, LPENTRYLIST lpEntries, LPENTRYLIST lpParents, ULONG flags, LPUNKNOWN lpCollector, LPSPropTagArray inclprop, LPSPropTagArray exclprop, ULONG ulBufferSize) _kc_override;
virtual HRESULT __stdcall GetChangeCount(ULONG *lpcChanges) _kc_override;
virtual HRESULT __stdcall SetMessageInterface(REFIID refiid) _kc_override;
virtual HRESULT __stdcall SetLogger(ECLogger *lpLogger) _kc_override;
