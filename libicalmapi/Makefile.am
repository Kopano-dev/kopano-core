# -*- Makefile -*-

lib_LTLIBRARIES = libkcicalmapi.la
noinst_DATA = libkcicalmapi.ldd
CLEANFILES = ${noinst_DATA}

AM_CPPFLAGS = ${ZCPPFLAGS} $(DEBUGFLAGS) $(BOOST_CPPFLAGS) \
	$(ICAL_CFLAGS) \
	-I$(top_srcdir)/mapi4linux/include -I$(top_srcdir)/provider/common \
	-I${top_srcdir}/common -I${top_srcdir}/m4lcommon \
	-I$(top_srcdir)/libfreebusy

libkcicalmapi_la_LDFLAGS = ${AM_LDFLAGS} \
	-no-undefined -version-info 0:0:0 \
	-Wl,--version-script=${top_builddir}/default.sym
EXTRA_libkcicalmapi_la_DEPENDENCIES = ${top_builddir}/default.sym

libkcicalmapi_la_LIBADD = \
	${top_builddir}/libfreebusy/libkcfreebusy.la \
	${top_builddir}/mapi4linux/src/libmapi.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/common/libkcutil.la \
	$(ICAL_LIBS)

libkcicalmapidir = $(includedir)/icalmapi
libkcicalmapi_HEADERS = icalitem.h nameids.h icaluid.h \
	MAPIToICal.h ICalToMAPI.h \
	vtimezone.h valarm.h vconverter.h vevent.h vtodo.h \
	icalrecurrence.h icalmapi.h vfreebusy.h

libkcicalmapi_la_SOURCES = icalinit.cpp \
	nameids.cpp \
	icaluid.cpp \
	MAPIToICal.cpp \
	ICalToMAPI.cpp \
	vtimezone.cpp \
	valarm.cpp \
	vconverter.cpp \
	vevent.cpp \
	vtodo.cpp \
	icalrecurrence.cpp \
	vfreebusy.cpp


check-syntax:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
		-fsyntax-only -fmessage-length=0 ${CHK_SOURCES} -Wall -Wformat=2

include ../global.am
