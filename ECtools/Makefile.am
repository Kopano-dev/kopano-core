# -*- Makefile -*-

SUBDIRS = . archiver/include/kopano backup presence search

AM_CPPFLAGS = ${ZCPPFLAGS} ${DEBUGFLAGS} ${BOOST_CPPFLAGS} ${SSL_CFLAGS} \
	-I${top_srcdir}/mapi4linux/include \
	-I${top_srcdir}/common -I${top_srcdir}/m4lcommon


bin_PROGRAMS = kopano-fsck kopano-passwd kopano-stats
dist_bin_SCRIPTS = utils/kopano-set-oof
sbin_PROGRAMS = kopano-admin kopano-archiver kopano-monitor
dist_sbin_SCRIPTS = utils/kopano-mailbox-permissions \
	archiver/scripts/kopano-archiver-aclset \
	archiver/scripts/kopano-archiver-aclsync \
	archiver/scripts/kopano-archiver-restore
noinst_PROGRAMS = kopano-cfgchecker
pkglibexec_PROGRAMS = mapitime
lib_LTLIBRARIES = libkcarchiver.la libkcarchivercore.la
noinst_DATA = \
	libkcarchiver.ldd libkcarchivercore.ldd \
	kopano-archiver.ldx kopano-cfgchecker.ldx \
	kopano-monitor.ldx kopano-passwd.ldx kopano-stats.ldx


kopano_admin_SOURCES = \
	admin/admin.cpp
kopano_admin_CPPFLAGS = \
	${AM_CPPFLAGS} -I${top_srcdir}/ECtools/archiver
kopano_admin_LDADD = \
	${top_builddir}/mapi4linux/src/libmapi.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/common/libkcutil.la \
	${top_builddir}/common/libkcssl.la \
	libkcarchiver.la ${PROG_LIBS} ${SSL_LIBS}

kopano_archiver_SOURCES = \
	archiver/main.cpp
kopano_archiver_CPPFLAGS = \
	${AM_CPPFLAGS} -I${top_srcdir}/provider/client \
	-I${top_srcdir}/provider/include \
	-I${top_builddir}/provider/soap -I${top_srcdir}/ECtools/archiver \
	${GSOAP_CFLAGS} ${MYSQL_INCLUDES}
kopano_archiver_LDADD = \
	${top_builddir}/mapi4linux/src/libmapi.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/common/libkcutil.la \
	libkcarchiver.la libkcarchivercore.la \
	${top_builddir}/common/libkcservice.la ${PROG_LIBS}


kopano_cfgchecker_SOURCES = \
	cfgchecker/cfg.cpp \
	cfgchecker/ECConfigCheck.cpp \
	cfgchecker/ECConfigCheck.h \
	cfgchecker/LDAPConfigCheck.cpp \
	cfgchecker/LDAPConfigCheck.h \
	cfgchecker/UnixConfigCheck.cpp \
	cfgchecker/UnixConfigCheck.h \
	cfgchecker/ServerConfigCheck.cpp \
	cfgchecker/ServerConfigCheck.h \
	cfgchecker/GatewayConfigCheck.cpp \
	cfgchecker/GatewayConfigCheck.h \
	cfgchecker/IcalConfigCheck.cpp \
	cfgchecker/IcalConfigCheck.h \
	cfgchecker/MonitorConfigCheck.cpp \
	cfgchecker/MonitorConfigCheck.h \
	cfgchecker/DAgentConfigCheck.cpp \
	cfgchecker/DAgentConfigCheck.h \
	cfgchecker/SpoolerConfigCheck.cpp \
	cfgchecker/SpoolerConfigCheck.h 
kopano_cfgchecker_LDADD = \
	${top_builddir}/common/libkcutil.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/mapi4linux/src/libmapi.la ${PROG_LIBS}


kopano_fsck_SOURCES = \
	fsck/fsck.cpp \
	fsck/fsck-main.cpp \
	fsck/fsck-calendar.cpp \
	fsck/fsck-contact.cpp \
	fsck/fsck-task.cpp \
	fsck/fsck.h
kopano_fsck_CPPFLAGS = \
	${AM_CPPFLAGS} -I${top_srcdir}/libicalmapi
kopano_fsck_LDADD = \
	${top_builddir}/mapi4linux/src/libmapi.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/common/libkcutil.la \
	${PROG_LIBS}


kopano_monitor_SOURCES = \
	monitor/monitor.cpp \
	monitor/ECMonitorDefs.h \
	monitor/ECQuotaMonitor.h \
	monitor/ECQuotaMonitor.cpp
kopano_monitor_LDADD = \
	${top_builddir}/mapi4linux/src/libmapi.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/common/libkcutil.la \
	${PROG_LIBS}


kopano_passwd_SOURCES = \
	passwd/passwd.cpp
kopano_passwd_LDADD = \
	${top_builddir}/mapi4linux/src/libmapi.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/common/libkcutil.la \
	${PROG_LIBS}


kopano_stats_SOURCES = \
	stats/stats.cpp
kopano_stats_CPPFLAGS = \
	${AM_CPPFLAGS} ${NCURSES_FLAGS} -I${top_srcdir}/libfreebusy
# ncurses needs to come first, because ICU_LIBS erroneously has -L/usr/lib64
# which overrides the ncurses lib selection >:-(
kopano_stats_LDADD = \
	${NCURSES_LIBS} \
	${top_builddir}/mapi4linux/src/libmapi.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/common/libkcutil.la \
	${PROG_LIBS}


libkcarchiver_la_SOURCES = \
	archiver/ArchiverSession.cpp \
	archiver/ArchiverSession.h \
	archiver/ArchiverSessionPtr.h \
	archiver/archiver-common.cpp \
	archiver/ArchiveManageImpl.cpp \
	archiver/ArchiveManageImpl.h \
	archiver/ArchiveManage.h \
	archiver/ArchiveStateCollector.cpp \
	archiver/ArchiveStateCollector.h \
	archiver/ArchiveStateUpdater.cpp \
	archiver/ArchiveStateUpdater.h \
	archiver/archivestateupdater_fwd.h \
	archiver/helpers/ArchiveHelper.cpp \
	archiver/helpers/ArchiveHelper.h \
	archiver/helpers/StoreHelper.cpp \
	archiver/helpers/StoreHelper.h \
	archiver/helpers/MAPIPropHelper.cpp \
	archiver/helpers/MAPIPropHelper.h \
	archiver/ECArchiverLogger.cpp \
	archiver/ECArchiverLogger.h
libkcarchiver_la_CPPFLAGS = ${kopano_archiver_CPPFLAGS}
libkcarchiver_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	-Wl,--version-script=${top_builddir}/default.sym
libkcarchiver_la_LIBADD = ${top_builddir}/mapi4linux/src/libmapi.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/common/libkcutil.la
EXTRA_libkcarchiver_la_DEPENDENCIES = ${top_builddir}/default.sym


libkcarchivercore_la_SOURCES = \
	archiver/Archiver.cpp \
	archiver/Archiver.h \
	archiver/AutoMAPI.h \
	archiver/ArchiverImpl.cpp \
	archiver/ArchiverImpl.h \
	archiver/operations/instanceidmapper.cpp \
	archiver/operations/instanceidmapper.h \
	archiver/operations/instanceidmapper_fwd.h \
	archiver/operations/operations.cpp \
	archiver/operations/operations.h \
	archiver/operations/operations_fwd.h \
	archiver/operations/copier.cpp \
	archiver/operations/copier.h \
	archiver/operations/deleter.cpp \
	archiver/operations/deleter.h \
	archiver/operations/stubber.cpp \
	archiver/operations/stubber.h \
	archiver/operations/transaction.cpp \
	archiver/operations/transaction.h \
	archiver/operations/transaction_fwd.h \
	archiver/operations/postsaveaction.h \
	archiver/operations/postsaveiidupdater.cpp \
	archiver/operations/postsaveiidupdater.h \
	archiver/ArchiveControlImpl.cpp \
	archiver/ArchiveControlImpl.h \
	archiver/ECDatabase.h \
	archiver/ECDatabase.cpp
libkcarchivercore_la_CPPFLAGS = ${kopano_archiver_CPPFLAGS}
libkcarchivercore_la_LDFLAGS = \
	${AM_LDFLAGS} -no-undefined \
	-Wl,--version-script=${top_builddir}/default.sym
libkcarchivercore_la_LIBADD = \
	${top_builddir}/mapi4linux/src/libmapi.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/common/libkcservice.la \
	libkcarchiver.la \
	${PROG_LIBS} ${SSL_LIBS} ${MYSQL_LIBS}
EXTRA_libkcarchivercore_la_DEPENDENCIES = \
	${top_builddir}/default.sym

mapitime_SOURCES = mapitime.cpp
mapitime_LDADD = -lrt ${top_builddir}/mapi4linux/src/libmapi.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/common/libkcutil.la

include ../global.am
