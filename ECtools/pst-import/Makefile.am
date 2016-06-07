# -*- Makefile -*-

sbin_PROGRAMS = kopano-pst-import

PST_LIBS =${top_builddir}/common/libkcmapi.la \
	${top_builddir}/common/libkcutil.la \
	${top_builddir}/mapi4linux/src/libmapi.la \
	-lpff \
	${PROG_LIBS} ${SSL_LIBS} ${BOOST_FILESYSTEM_LIBS} ${BOOST_SYSTEM_LIBS}

PST_SOURCES = import.cpp

AM_CPPFLAGS = ${ZCPPFLAGS} ${DEBUGFLAGS} ${BOOST_CPPFLAGS} \
	-I${top_srcdir}/mapi4linux/include \
	${zcp_CFLAGS}

kopano_pst_import_LDADD = ${zcp_LIBS} ${PST_LIBS}
kopano_pst_import_SOURCES = ${PST_SOURCES}

include ../../global.am
