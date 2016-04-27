# -*- Makefile -*-

SUBDIRS = include/kopano

AM_CPPFLAGS = ${ZCPPFLAGS} ${DEBUGFLAGS} ${BOOST_CPPFLAGS} ${SSL_CFLAGS} \
	-I${top_srcdir}/m4lcommon \
	-I${top_srcdir}/mapi4linux/include -I${top_srcdir}/provider/common \
	-I${top_srcdir}/libfreebusy ${MYSQL_INCLUDES}

pkgconfdir = ${libdir}/pkgconfig
pkgconf_DATA = kopano.pc

lib_LTLIBRARIES = \
	libkcutil.la libkcmapi.la \
	libkcssl.la libkcservice.la
noinst_DATA = \
	libkcutil.ldd libkcmapi.ldd \
	libkcssl.ldd libkcservice.ldd

#
# libkcutil
#
noinst_HEADERS = \
	ConsoleTable.h ECACL.h ECCache.h ECConfigImpl.h \
	ECFifoBuffer.h ECSerializer.h StatsClient.h \
	SymmetricCrypt.h TimeUtil.h TmpPath.h \
	fileutil.h pthreadutil.h utf8.h \
	charset/utf32string.h charset/localeutil.h \
	ustringutil/utfutil.h \
	utf8/checked.h utf8/core.h utf8/unchecked.h
libkcutil_la_SOURCES = \
	ConsoleTable.cpp ECACL.cpp ECCache.cpp ECConfig.cpp ECConfigImpl.cpp \
	ECFifoBuffer.cpp ECGetText.cpp ECIConv.cpp ECLogger.cpp \
	ECScheduler.cpp ECSerializer.cpp ECThreadPool.cpp StatsClient.cpp \
	SymmetricCrypt.cpp TimeUtil.cpp TmpPath.cpp Trace.cpp UnixUtil.cpp \
	base64.cpp fileutil.cpp md5.cpp my_getopt.cpp \
	platform.cpp platform.linux.cpp \
	stringutil.cpp threadutil.cpp ustringutil.cpp \
	charset/convert.cpp charset/convstring.cpp charset/traits.cpp \
	charset/localeutil.cpp
libkcutil_la_LIBADD = \
	-lpthread -lz ${BOOST_FILESYSTEM_LIBS} ${CRYPTO_LIBS} ${UUID_LIBS}
libkcutil_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	-Wl,--version-script=${top_builddir}/default.sym
EXTRA_libkcutil_la_DEPENDENCIES = ${top_builddir}/default.sym

if HAVE_ICU
libkcutil_la_SOURCES += ustringutil/utfutil.cpp
libkcutil_la_LIBADD += ${icu_uc_LIBS} ${icu_i18n_LIBS}
endif

#
# libkcmapi
#
noinst_HEADERS += \
	ECFifoStream.h ECMemStream.h \
	HtmlEntity.h HtmlToTextParser.h IStreamAdapter.h \
	rtfutil.h
libkcmapi_la_SOURCES = \
	ECABEntryID.cpp ECDebug.cpp ECDebugPrint.cpp ECGuid.cpp \
	ECFifoStream.cpp ECKeyTable.cpp ECMemStream.cpp ECUnknown.cpp \
	HtmlEntity.cpp HtmlToTextParser.cpp IStreamAdapter.cpp \
	MAPIErrors.cpp kcodes.cpp codepage.cpp rtfutil.cpp
libkcmapi_la_LIBADD = libkcutil.la -lpthread
libkcmapi_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	-Wl,--version-script=${top_builddir}/default.sym
EXTRA_libkcmapi_la_DEPENDENCIES = ${top_builddir}/default.sym

#
# libkcssl
#
noinst_HEADERS += ECChannelClient.h SSLUtil.h
libkcssl_la_SOURCES = ECChannel.cpp ECChannelClient.cpp SSLUtil.cpp
libkcssl_la_LIBADD = libkcutil.la -lpthread ${SSL_LIBS}
libkcssl_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	-Wl,--version-script=${top_builddir}/default.sym
EXTRA_libkcssl_la_DEPENDENCIES = ${top_builddir}/default.sym

#
# libkcservice
#
noinst_HEADERS += ECDatabaseMySQL.h
libkcservice_la_SOURCES = ECDatabaseMySQL.cpp
libkcservice_la_LIBADD = libkcutil.la -lpthread ${MYSQL_LIBS}
libkcservice_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	-Wl,--version-script=${top_builddir}/default.sym
EXTRA_libkcservice_la_DEPENDENCIES = ${top_builddir}/default.sym

check_PROGRAMS = cov-scope
cov_scope_SOURCES = cov-scope.cpp


# Create historic symlinks (e.g. older snapshots of zstatsd use it) for build
install-exec-hook:
	${MKDIR_P} ${DESTDIR}${pkglibdir}
	ln -fs ${libdir}/libkcutil.so ${DESTDIR}${pkglibdir}/libcommon_util.so
	ln -fs ${libdir}/libkcmapi.so ${DESTDIR}${pkglibdir}/libcommon_mapi.so
	ln -fs ${libdir}/libkcssl.so ${DESTDIR}${pkglibdir}/libcommon_ssl.so
	ln -fs ${libdir}/libkcservice.so ${DESTDIR}${pkglibdir}/libcommon_service.so

check-syntax:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
		-fsyntax-only -fmessage-length=0 ${CHK_SOURCES} -Wall -Wformat=2

include ../global.am
