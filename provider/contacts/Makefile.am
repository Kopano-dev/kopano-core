# -*- Makefile -*-

pkglib_LTLIBRARIES = libkccontacts.la
noinst_DATA = libkccontacts.ldd

sysconf_configdir = ${MAPICONFIGDIR}
dist_sysconf_config_DATA = zcontacts.inf

sampleconfdir = ${docdir}/example-config
sampleconf_DATA = zcontacts.inf

AM_CPPFLAGS = ${ZCPPFLAGS} $(DEBUGFLAGS)	\
	-I${top_srcdir}/provider/include	\
	-I${top_srcdir}/provider/common 	\
	-I${top_srcdir}/provider/client		\
	-I${top_srcdir}/mapi4linux/include	\
	-I${top_srcdir}/common -I${top_srcdir}/m4lcommon

libkccontacts_la_LDFLAGS = ${AM_LDFLAGS} -avoid-version -module
libkccontacts_la_LIBADD = \
	${top_builddir}/common/libkcutil.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/mapi4linux/src/libmapi.la			\
	-lstdc++ $(UUID_LIBS) $(ICU_LIBS)

libkccontacts_la_SOURCES = epcontact.cpp ZCABData.h \
	ZCMAPIProp.h ZCMAPIProp.cpp \
	ZCABProvider.h ZCABProvider.cpp \
	ZCABLogon.h ZCABLogon.cpp \
	ZCABContainer.h ZCABContainer.cpp

check-syntax:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
		-fsyntax-only -fmessage-length=0 ${CHK_SOURCES} -Wall -Wformat=2

include ../../global.am
