# -*- Makefile -*-

AM_CPPFLAGS = ${ZCPPFLAGS} $(DEBUGFLAGS) $(BOOST_CPPFLAGS) \
	-I${top_srcdir}/mapi4linux/include \
	-I${top_srcdir}/libfreebusy \
	-I${top_srcdir}/inetmapi \
	-I${top_srcdir}/common -I${top_srcdir}/m4lcommon \
	$(PHP_INCLUDES)
AM_CXXFLAGS = ${ZCXXFLAGS} ${ZPHPCXXFLAGS}

phplib_LTLIBRARIES = mapi.la

phplibdir = ${dcprefix}${PHP_EXTENSION_DIR}

mapi_la_LDFLAGS = ${AM_LDFLAGS} -module -avoid-version \
	$(PHP_LDFLAGS)

mapi_la_LIBADD = \
	${top_builddir}/libmapi.la \
	${top_builddir}/libkcmapi.la \
	${top_builddir}/libkcutil.la \
	${top_builddir}/libkcfreebusy.la \
	${top_builddir}/libkcinetmapi.la \
	$(PROG_LIBS) -lpthread

# it seems linking libraries with -pthread will not contain -lpthread.
# but because php is not linked against -lpthread, the library will not be loaded,
# and be missing at runtime.
# therefore, we explicitly link mapi.so with -lpthread :(

dist_data_DATA = include/mapi/mapicode.php include/mapi/mapidefs.php include/mapi/mapitags.php include/mapi/mapi.util.php include/mapi/class.recurrence.php include/mapi/class.meetingrequest.php include/mapi/class.freebusypublish.php include/mapi/class.baseexception.php include/mapi/class.mapiexception.php include/mapi/mapiguid.php include/mapi/class.baserecurrence.php include/mapi/class.taskrequest.php include/mapi/class.taskrecurrence.php
datadir = ${datarootdir}/php/mapi

#
# Debian has a broken php-config(1) script which outputs
# 	"--with-config-file-scan-dir=/etc/php5/apache2/conf.d"
# If we install our config file into that location, it will only be available
# to only apache2-mod_php5 and not php-cli, php-fpm, etc.
#
phpconfdir = ${dcprefix}${PHP_SYSCONF_DIR}
dist_phpconf_DATA = mapi.ini

sampleconfdir = ${docdir}/example-config
sampleconf_DATA = mapi.ini

mapi_la_SOURCES = main.cpp util.cpp \
	phpconfig.h main.h util.h \
	ECImportContentsChangesProxy.cpp ECImportContentsChangesProxy.h \
	ECImportHierarchyChangesProxy.cpp ECImportHierarchyChangesProxy.h \
	ECRulesTableProxy.cpp ECRulesTableProxy.h \
	typeconversion.cpp typeconversion.h \
	globals.h

check-syntax:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
		-fsyntax-only -fmessage-length=0 ${CHK_SOURCES} -Wall -Wformat=2 -Wno-strict-aliasing
