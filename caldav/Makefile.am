# -*- Makefile -*-

AM_CPPFLAGS = ${ZCPPFLAGS} ${DEBUGFLAGS} ${BOOST_CPPFLAGS} ${SSL_CFLAGS} \
	-I${top_srcdir}/mapi4linux/include \
	-I${top_srcdir}/inetmapi -I${top_srcdir}/provider/common \
	-I${top_srcdir}/common -I${top_srcdir}/m4lcommon \
	-I${top_srcdir}/gateway -I${top_srcdir}/libicalmapi -I${top_srcdir}/libfreebusy \
	$(XML2_CFLAGS) $(ICAL_CFLAGS)

sbin_PROGRAMS = kopano-ical
noinst_DATA = kopano-ical.ldx

kopano_ical_SOURCES = CalDAV.cpp \
	CalDavProto.cpp CalDavProto.h \
	Http.cpp Http.h \
	iCal.cpp iCal.h \
	CalDavUtil.cpp CalDavUtil.h \
	WebDav.cpp WebDav.h \
	ProtocolBase.cpp ProtocolBase.h	

kopano_ical_LDADD = ${top_builddir}/mapi4linux/src/libmapi.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/common/libkcutil.la \
	${top_builddir}/libicalmapi/libkcicalmapi.la \
	${top_builddir}/common/libkcssl.la \
	${top_builddir}/libfreebusy/libkcfreebusy.la \
	$(PROG_LIBS) $(SSL_LIBS) $(ICONV_LIBS) $(XML2_LIBS) $(ICAL_LIBS) \
	${icu_uc_LIBS}

check-syntax:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
		-fsyntax-only -fmessage-length=0 ${CHK_SOURCES} -Wall -Wformat=2

include ../global.am
