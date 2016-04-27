# -*- Makefile -*-

AM_CPPFLAGS = ${ZCPPFLAGS} ${DEBUGFLAGS} ${GSOAP_CFLAGS} \
	${BOOST_CPPFLAGS} \
	-I${top_srcdir}/mapi4linux/include -I${top_srcdir}/provider/include \
	-I${top_builddir}/provider/soap -I${top_srcdir}/provider/soap \
	-I${top_srcdir}/common -I${top_srcdir}/m4lcommon

pkginclude_HEADERS = include/kopano/pcuser.hpp

noinst_LTLIBRARIES = libkccommon.la
#noinst_DATA = libkccommon.ldd

libkccommon_la_SOURCES = SOAPUtils.cpp SOAPUtils.h \
	SOAPAlloc.h \
	SOAPHelpers.cpp SOAPHelpers.h \
	SOAPDebug.cpp SOAPDebug.h \
	SOAPSock.cpp SOAPSock.h \
	pcutil.cpp pcutil.hpp \
	pcuser.cpp \
	ECLicenseClient.h ECLicenseClient.cpp \
	ECSearchClient.h ECSearchClient.cpp \
	ECOfflineState.h ECOfflineState.cpp
libkccommon_la_LIBADD = \
	../../common/libkcutil.la ../../common/libkcmapi.la \
	../soap/libkcsoapclient.la ../../common/libkcssl.la \
	${GSOAP_LIBS} ${SSL_LIBS}
libkccommon_la_LDFLAGS = ${AM_LDFLAGS} \
	-Wl,--version-script=${top_builddir}/default.sym
EXTRA_libkccommon_la_DEPENDENCIES = ${top_builddir}/default.sym

check-syntax:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
		-fsyntax-only -fmessage-length=0 ${CHK_SOURCES} -Wall -Wformat=2

include ../../global.am
