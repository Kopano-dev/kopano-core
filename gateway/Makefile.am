# -*- Makefile -*-

AM_CPPFLAGS = ${ZCPPFLAGS} ${DEBUGFLAGS} \
	${BOOST_CPPFLAGS} ${CRYPTO_CFLAGS} ${SSL_CFLAGS} \
	-I${top_srcdir}/mapi4linux/include \
	-I${top_srcdir}/inetmapi -I${top_srcdir}/provider/common \
	-I${top_srcdir}/common -I${top_srcdir}/m4lcommon

sbin_PROGRAMS = kopano-gateway
noinst_DATA = kopano-gateway.ldx
CLEANFILES = ${noinst_DATA}

kopano_gateway_LDADD = ${top_builddir}/inetmapi/libkcinetmapi.la \
	${top_builddir}/mapi4linux/src/libmapi.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/common/libkcutil.la \
	${top_builddir}/common/libkcssl.la \
	${PROG_LIBS} ${CRYPTO_LIBS} ${SSL_LIBS} ${XML2_LIBS} ${icu_uc_LIBS}

kopano_gateway_SOURCES = Gateway.cpp POP3.cpp POP3.h IMAP.cpp IMAP.h ClientProto.h

check-syntax:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
		-fsyntax-only -fmessage-length=0 ${CHK_SOURCES} -Wall -Wformat=2

include ../global.am
