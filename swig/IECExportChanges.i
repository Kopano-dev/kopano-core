/* SPDX-License-Identifier: AGPL-3.0-only */
%{
#include <kopano/IECInterfaces.hpp>
%}

class IECExportChanges : public IExchangeExportChanges{
public:
	virtual HRESULT GetChangeCount(ULONG *lpcChanges) = 0;
	%extend {
		~IECExportChanges() { self->Release(); };
	}
};

