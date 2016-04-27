%apply (unsigned char *, unsigned int)					{(unsigned char *lpData, unsigned int ulSize)}

%include <typemaps/cstrings.swg>
%typemaps_cstring(%bstring,
		 unsigned char,
		 SWIG_AsCharPtr,
		 SWIG_AsCharPtrAndSize,
		 SWIG_FromCharPtr,
		 SWIG_FromBytePtrAndSize);
%bstring_output_allocate_size(unsigned char **lpAuthResponse, unsigned int *lpulResponseSize, delete[] *$1)

class IECLicense : public IUnknown {
public:
	virtual HRESULT LicenseAuth(unsigned char *lpData, unsigned int ulSize, unsigned char **lpAuthResponse, unsigned int *lpulResponseSize) = 0;

	/**
	 * The following two methods are displayed for completeness.
	 *
	 * virtual HRESULT LicenseCapa(unsigned int ulServiceType, char ***lppszCapabilities, unsigned int *lpulCapabilities) = 0;
	 * virtual HRESULT LicenseUsers(unsigned int ulServiceType, unsigned int *ulUsers) = 0;
	 */

	%extend {
		~IECLicense() { self->Release(); }
	}
};
