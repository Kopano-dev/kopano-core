class IECMultiStoreTable : public IUnknown {
public:
	/* ulFlags is currently unused */
	virtual HRESULT OpenMultiStoreTable(LPENTRYLIST lpMsgList, ULONG ulFlags, IMAPITable **OUTPUT) = 0;

	%extend {
		~IECMultiStoreTable() { self->Release(); }
	}
};
