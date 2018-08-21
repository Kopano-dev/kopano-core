/* SPDX-License-Identifier: AGPL-3.0-only */
class IECSpooler : public virtual IUnknown {
public:
	// Gets an IMAPITable containing all the outgoing messages on the server
	virtual HRESULT GetMasterOutgoingTable(ULONG ulFlags, IMAPITable **lppTable) = 0;

	// Removes a message from the master outgoing table
	virtual HRESULT DeleteFromMasterOutgoingTable(ULONG cbEntryID, LPENTRYID lpEntryID, ULONG ulFlags) = 0;

	%extend {
		~IECSpooler() { self->Release(); }
	}
};
