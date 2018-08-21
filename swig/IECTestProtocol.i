/* SPDX-License-Identifier: AGPL-3.0-only */
%include "cstring.i"
%apply (unsigned int, char **) {(unsigned int argc, char **args)}

%cstring_output_allocate(char **OUTPUT, MAPIFreeBuffer(*$1))

class IECTestProtocol : public virtual IUnknown {
public:
	virtual HRESULT TestPerform(const char *cmd, unsigned int argc, char **args) = 0;
	virtual HRESULT TestSet(const char *name, char *value) = 0;
	virtual HRESULT TestGet(const char *name, char **OUTPUT) = 0;

	%extend {
		~IECTestProtocol() { self->Release(); }
	}
};
