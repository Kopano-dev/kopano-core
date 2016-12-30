virtual HRESULT HrReadProps(LPSPropTagArray *lppPropTags,ULONG *cValues, LPSPropValue *lppValues) _kc_override;
virtual HRESULT HrLoadProp(ULONG ulObjId, ULONG ulPropTag, LPSPropValue *lppsPropValue) _kc_override;
virtual	HRESULT	HrWriteProps(ULONG cValues, LPSPropValue lpValues, ULONG flags = 0) _kc_override;
virtual HRESULT HrDeleteProps(const SPropTagArray *lpsPropTagArray) _kc_override;
virtual HRESULT HrSaveObject(ULONG flags, MAPIOBJECT *lpsMapiObject) _kc_override;
virtual HRESULT HrLoadObject(MAPIOBJECT **lppsMapiObject) _kc_override;
virtual IECPropStorage *GetServerStorage(void) _kc_override;
