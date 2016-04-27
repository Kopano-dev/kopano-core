# -*- Makefile -*-

AM_CPPFLAGS = ${ZCPPFLAGS} $(DEBUGFLAGS) $(BOOST_CPPFLAGS) \
	-DMAPICONFIGDIR=\"${MAPICONFIGDIR}\" \
	-DPKGLIBDIR=\"${pkglibdir}\" \
	-I$(top_srcdir)/mapi4linux/include -I$(top_srcdir)/provider/common \
	-I${top_srcdir}/common -I${top_srcdir}/m4lcommon \
	-I${top_srcdir}/libfreebusy

lib_LTLIBRARIES = libmapi.la
noinst_DATA = libmapi.ldd

libmapi_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined -version-info 1:0:0 \
	-Wl,--version-script=libmapi.sym ${BOOST_FILESYSTEM_LDFLAGS}
EXTRA_libmapi_la_DEPENDENCIES = libmapi.sym

libmapi_la_LIBADD = \
	${top_builddir}/common/libkcutil.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/m4lcommon/libm4lcommon.la \
	$(BOOST_FILESYSTEM_LIBS) \
	$(BOOST_SYSTEM_LIBS) \
	$(PROG_LIBS) \
	$(DL_LIBS)

libmapi_la_SOURCES = common.cpp mapidefs.cpp mapispi.cpp mapiutil.cpp mapix.cpp rtf.cpp imessage.cpp \
	m4l.common.h m4l.mapidefs.h m4l.mapispi.h m4l.mapiutil.h m4l.mapix.h mapi4linux.h m4l.debug.h rtf.h \
	m4l.mapisvc.cpp m4l.mapisvc.h

check-syntax:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
		-fsyntax-only -fmessage-length=0 ${CHK_SOURCES} -Wall -Wformat=2

libmapi.sym: ${top_builddir}/config.status

include ../../global.am
