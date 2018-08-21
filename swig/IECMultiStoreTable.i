/* SPDX-License-Identifier: AGPL-3.0-only */
class IECMultiStoreTable : public virtual IUnknown {
public:
	/* ulFlags is currently unused */
	virtual HRESULT OpenMultiStoreTable(const ENTRYLIST *msglist, ULONG flags, IMAPITable **OUTPUT) = 0;

	%extend {
		~IECMultiStoreTable() { self->Release(); }
	}
};
