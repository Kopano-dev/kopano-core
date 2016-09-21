ACLOCAL_AMFLAGS = -I autoconf


SUBDIRS = . common m4lcommon mapi4linux libfreebusy libsync \
          provider libicalmapi inetmapi
if WITH_PHP5
SUBDIRS += php-ext
endif
if WITH_PHP7
SUBDIRS += php7-ext
endif
SUBDIRS += ECtools spooler gateway caldav installer po doc swig

EXTRA_DIST = version versionheader.sh \
	LICENSE.txt CONTRIBUTORS.txt AGPL-3 \
	lddcheck global.am pwdcheck.sh

BUILT_SOURCES = source-moved-check common/include/kopano/ecversion.h

sbin_SCRIPTS = tools/python-scripts/kopano-cachestat.py
gdbautoloaddir = ${datadir}/gdb/auto-load/${libdir}
gdbautoload_DATA = tools/libmapi.so.1.0.0-gdb.py

.PHONY: source-moved-check
source-moved-check:
	@${top_srcdir}/pwdcheck.sh "${abs_top_builddir}"

common/include/kopano/ecversion.h: versionheader.sh
	sh ${top_srcdir}/versionheader.sh >$@

dist-hook:
	find $(distdir) -type d -name .svn -exec rm -rf {} \; 2>/dev/null || true

default.sym: ${top_builddir}/config.status
