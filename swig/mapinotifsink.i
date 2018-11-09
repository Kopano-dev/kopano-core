/* SPDX-License-Identifier: AGPL-3.0-only */
// Force constructor on MAPINotifSink even though we're not exposing all abstract
// methods
%feature("notabstract") MAPINotifSink;

class MAPINotifSink : public IMAPIAdviseSink {
public:
	HRESULT GetNotifications(ULONG *OUTPUTC, LPNOTIFICATION *OUTPUTP, BOOL fNonBlock, ULONG timeout_msec);
	%extend {
		MAPINotifSink() {
			MAPINotifSink *lpSink = NULL;
			MAPINotifSink::Create(&lpSink);
			return lpSink;
		};
		~MAPINotifSink() { self->Release(); }
	}
};
