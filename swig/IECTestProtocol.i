%include "cstring.i"
%apply (unsigned int, char **) {(unsigned int ulArgs, char **szArgs)}

%cstring_output_allocate(char **OUTPUT, MAPIFreeBuffer(*$1))

class IECTestProtocol : public IUnknown {
public:
    virtual HRESULT TestPerform(char *szCommand, unsigned int ulArgs, char **szArgs) = 0;
	virtual HRESULT TestSet(char *szName, char *szValue) = 0;
	virtual HRESULT TestGet(char *szName, char **OUTPUT) = 0;

	%extend {
		~IECTestProtocol() { self->Release(); }
	}
};
