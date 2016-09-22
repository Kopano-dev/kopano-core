# -*- Makefile -*-

AM_CPPFLAGS = ${ZCPPFLAGS} ${DEBUGFLAGS} ${BOOST_CPPFLAGS} \
	-I${top_srcdir}/mapi4linux/include -I${top_srcdir}/provider/common \
	-I${top_srcdir}/common -I${top_srcdir}/m4lcommon

lib_LTLIBRARIES = libkcfreebusy.la
noinst_DATA = libkcfreebusy.ldd
CLEANFILES = ${noinst_DATA}

libkcfreebusy_la_includedir = $(includedir)/libfreebusy

libkcfreebusy_la_include_HEADERS = ECEnumFBBlock.h ECFBBlockList.h		\
	ECFreeBusyData.h ECFreeBusySupport.h ECFreeBusyUpdate.h freebusy.h	\
	freebusyguid.h freebusytags.h freebusyutil.h PublishFreeBusy.h recurrence.h

libkcfreebusy_la_SOURCES = ECEnumFBBlock.cpp ECFBBlockList.cpp		\
	ECFreeBusyData.cpp ECFreeBusySupport.cpp ECFreeBusyUpdate.cpp	\
	freebusyutil.cpp PublishFreeBusy.cpp recurrence.cpp
libkcfreebusy_la_LIBADD = \
	${top_builddir}/common/libkcutil.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/mapi4linux/src/libmapi.la
libkcfreebusy_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	-Wl,--version-script=${top_builddir}/default.sym
EXTRA_libkcfreebusy_la_DEPENDENCIES = ${top_builddir}/default.sym

check-syntax:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
		-fsyntax-only -fmessage-length=0 ${CHK_SOURCES} -Wall -Wformat=2

include ../global.am
