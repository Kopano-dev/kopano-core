# -*- Makefile -*-

SUBDIRS = include/inetmapi

AM_CPPFLAGS = ${ZCPPFLAGS} $(DEBUGFLAGS) $(BOOST_CPPFLAGS) \
	$(VMIME_CFLAGS) -I${srcdir}/include \
	-I$(top_srcdir)/mapi4linux/include -I$(top_srcdir)/provider/common \
	-I${top_srcdir}/common -I${top_srcdir}/m4lcommon \
	-I${top_srcdir}/libfreebusy -I${top_srcdir}/libicalmapi \
	${XML2_CFLAGS}

lib_LTLIBRARIES = libkcinetmapi.la
noinst_DATA = libkcinetmapi.ldd

# Add '-z now' -> -Wl,-z, -Wl,now to LD flags so that we do early binding of symbols. This fixes
# a problem in which libvmime attempts to resolve a symbol in libinetmapi.so during the unload
# of vmime, initiated by the unload of libinetmapi. On some systems, this fails to resolve the symbol
# during unload, resulting in a crash in __cxa_finalize during unload of mapi.so
libkcinetmapi_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	-version-info 0:0:0 -Wl,-z -Wl,now \
	-Wl,--version-script=${top_builddir}/default.sym
EXTRA_libkcinetmapi_la_DEPENDENCIES = ${top_builddir}/default.sym

libkcinetmapi_la_LIBADD = \
	${top_builddir}/mapi4linux/src/libmapi.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/common/libkcutil.la \
	${top_builddir}/libicalmapi/libkcicalmapi.la \
    $(VMIME_LIBS) $(XML2_LIBS)

#CXXLD=--tag=none gcc

libinetmapidir = $(includedir)/inetmapi
libinetmapi_HEADERS = ECMapiUtils.h ECVMIMEUtils.h	\
		MAPIToVMIME.h VMIMEToMAPI.h \
		outputStreamMAPIAdapter.h					\
		inputStreamMAPIAdapter.h					\
		mapiAttachment.h tnef.h						\
		mapiTextPart.h								\
		MAPISMTPTransport.h							\
		SMIMEMessage.h

libkcinetmapi_la_SOURCES = ECMapiUtils.cpp ECVMIMEUtils.cpp \
		MAPIToVMIME.cpp VMIMEToMAPI.cpp						\
		outputStreamMAPIAdapter.cpp							\
		inputStreamMAPIAdapter.cpp							\
		mapiAttachment.cpp									\
		mapiTextPart.cpp									\
		MAPISMTPTransport.cpp								\
		inetmapi.cpp tnef.cpp								\
		SMIMEMessage.cpp										\
		serviceRegistration.inl

check-syntax:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
		-fsyntax-only -fmessage-length=0 ${CHK_SOURCES} -Wall -Wformat=2

include ../global.am
