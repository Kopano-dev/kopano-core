/* SPDX-License-Identifier: AGPL-3.0-only */
%apply (unsigned char *, unsigned int)					{(unsigned char *lpData, unsigned int ulSize)}

%include <typemaps/cstrings.swg>
%typemaps_cstring(%bstring,
		 unsigned char,
		 SWIG_AsCharPtr,
		 SWIG_AsCharPtrAndSize,
		 SWIG_FromCharPtr,
		 SWIG_FromBytePtrAndSize);
%bstring_output_allocate_size(unsigned char **lpAuthResponse, unsigned int *lpulResponseSize, delete[] *$1)
