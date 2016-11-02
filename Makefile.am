# -*- Makefile -*-

ACLOCAL_AMFLAGS = -I autoconf

SUBDIRS = . ECtools/archiver/include/kopano ECtools/backup ECtools/presence \
	ECtools/migration-pst \
	ECtools/search common/include/kopano m4lcommon/include/kopano \
	mapi4linux/include inetmapi/include/inetmapi provider/include \
	spooler/python
if WITH_PHP5
SUBDIRS += php-ext
endif
if WITH_PHP7
SUBDIRS += php7-ext
endif
SUBDIRS += installer po swig


AM_CPPFLAGS = ${ZCPPFLAGS} ${DEBUGFLAGS} -DLDAP_THREAD_SAFE \
	-DMAPICONFIGDIR=\"${MAPICONFIGDIR}\" -DPKGLIBDIR='"${pkglibdir}"' \
	-Iprovider/soap \
	-I${top_srcdir}/common \
	-I${top_srcdir}/libfreebusy \
	-I${top_srcdir}/libsync \
	-I${top_srcdir}/m4lcommon \
	-I${top_srcdir}/mapi4linux/include \
	-I${top_srcdir}/provider/client \
	-I${top_srcdir}/provider/common \
	-I${top_srcdir}/provider/include \
	-I${top_srcdir}/provider/libserver \
	-I${top_srcdir}/provider/plugins \
	-I${top_srcdir}/libfreebusy \
	-I${top_srcdir}/libicalmapi \
	-I${top_srcdir}/ECtools/archiver \
	${BOOST_CPPFLAGS} ${CRYPTO_CFLAGS} ${GSOAP_CFLAGS} ${ICAL_CFLAGS} \
	${KRB5_CFLAGS} ${LDAP_FLAGS} ${MYSQL_INCLUDES} ${SSL_CFLAGS} \
	${s3_CFLAGS} ${TCMALLOC_CFLAGS} ${VMIME_CFLAGS} ${XML2_CFLAGS}
AM_CXXFLAGS = ${ZCXXFLAGS} -Wno-sign-compare


#
# target programs
#
bin_PROGRAMS = kopano-fsck kopano-passwd kopano-stats
dist_bin_SCRIPTS = ECtools/utils/kopano-set-oof
sbin_PROGRAMS = \
	kopano-admin kopano-archiver kopano-dagent kopano-gateway kopano-ical \
	kopano-monitor kopano-server kopano-spooler
dist_sbin_SCRIPTS = ECtools/utils/kopano-mailbox-permissions \
	ECtools/archiver/scripts/kopano-archiver-aclset \
	ECtools/archiver/scripts/kopano-archiver-aclsync \
	ECtools/archiver/scripts/kopano-archiver-restore
noinst_PROGRAMS = kopano-cfgchecker
check_PROGRAMS = cov-scope
pkglibexec_PROGRAMS = mapitime


#
# target libraries
#
# `make install` ignores both *_LIBADD, but also calls libtool for relinking,
# so the order of lib_LTLIBRARIES is important! Dependant libs go to the right.
# (Order was previously implied by the SUBDIRS variable.)
#
noinst_LTLIBRARIES = libkccommon.la libm4lcommon.la
lib_LTLIBRARIES = \
	libkcutil.la libkcmapi.la libkcssl.la libmapi.la libkchl.la \
	libkcfreebusy.la libkcsync.la \
	libkcsoapclient.la libkcsoapserver.la libkcserver.la \
	libkcicalmapi.la libkcinetmapi.la \
	libkcarchiver.la libkcarchivercore.la
noinst_DATA = \
	kopano-archiver.ldx kopano-cfgchecker.ldx kopano-dagent.ldx \
	kopano-gateway.ldx kopano-ical.ldx kopano-monitor.ldx \
	kopano-passwd.ldx kopano-server.ldx kopano-spooler.ldx \
	kopano-stats.ldx \
	libkcarchiver.ldd libkcarchivercore.ldd libkcclient.ldd \
	libkccontacts.ldd libkcfreebusy.ldd libkchl.ldd libkcicalmapi.ldd \
	libkcinetmapi.ldd libkcmapi.ldd libkcssl.ldd \
	libkcsync.ldd libkcutil.ldd libmapi.ldd
#noinst_DATA += libkccommon.ldd libkcserver.ldd \
#	libkcsoapclient.ldd libkcsoapserver.ldd
pkglib_LTLIBRARIES = libkcclient.la libkccontacts.la


#
# headers and other static files
#
pkginclude_HEADERS = \
	provider/common/include/kopano/pcuser.hpp \
	provider/libserver/include/kopano/ECPluginSharedData.h
noinst_HEADERS =
pkgconfdir = ${libdir}/pkgconfig
pkgconf_DATA = common/kopano.pc
sysconf_configdir = ${MAPICONFIGDIR}
dist_sysconf_config_DATA = provider/client/kopano.inf provider/contacts/zcontacts.inf
sampleconfdir = ${docdir}/example-config
sampleconf_DATA = provider/client/kopano.inf provider/contacts/zcontacts.inf
BUILT_SOURCES =
CLEANFILES = ${noinst_DATA}
EXTRA_DIST =
default_sym = ${top_builddir}/default.sym


#
# kopano-admin
#
kopano_admin_SOURCES = ECtools/admin/admin.cpp
kopano_admin_LDADD = \
	libmapi.la libkcmapi.la libkcutil.la libkcssl.la libkcarchiver.la \
	${PROG_LIBS} ${SSL_LIBS}


#
# kopano-archiver
#
kopano_archiver_SOURCES = ECtools/archiver/main.cpp
kopano_archiver_LDADD = \
	libmapi.la libkcmapi.la libkcutil.la libkcarchiver.la \
	libkcarchivercore.la ${PROG_LIBS}


#
# kopano-cfgchecker
#
kopano_cfgchecker_SOURCES = \
	ECtools/cfgchecker/DAgentConfigCheck.cpp ECtools/cfgchecker/DAgentConfigCheck.h \
	ECtools/cfgchecker/ECConfigCheck.cpp ECtools/cfgchecker/ECConfigCheck.h \
	ECtools/cfgchecker/GatewayConfigCheck.cpp ECtools/cfgchecker/GatewayConfigCheck.h \
	ECtools/cfgchecker/IcalConfigCheck.cpp ECtools/cfgchecker/IcalConfigCheck.h \
	ECtools/cfgchecker/LDAPConfigCheck.cpp ECtools/cfgchecker/LDAPConfigCheck.h \
	ECtools/cfgchecker/MonitorConfigCheck.cpp ECtools/cfgchecker/MonitorConfigCheck.h \
	ECtools/cfgchecker/ServerConfigCheck.cpp ECtools/cfgchecker/ServerConfigCheck.h \
	ECtools/cfgchecker/SpoolerConfigCheck.cpp ECtools/cfgchecker/SpoolerConfigCheck.h \
	ECtools/cfgchecker/UnixConfigCheck.cpp ECtools/cfgchecker/UnixConfigCheck.h \
	ECtools/cfgchecker/cfg.cpp
kopano_cfgchecker_LDADD = libkcutil.la libkcmapi.la libmapi.la ${PROG_LIBS}


#
# kopano-dagent
#
kopano_dagent_CPPFLAGS = \
	${AM_CPPFLAGS} -Ispooler ${PYTHON_INCLUDES} \
	-DSWIG_TYPE_TABLE=MAPI
kopano_dagent_SOURCES = \
	spooler/DAgent.cpp spooler/LMTP.cpp spooler/LMTP.h \
	spooler/PyMapiPlugin.cpp spooler/PyMapiPlugin.h \
	spooler/PythonSWIGRuntime.h spooler/archive.cpp spooler/archive.h \
	spooler/rules.cpp spooler/rules.h spooler/spmain.h
kopano_dagent_LDADD = \
	libkcinetmapi.la libmapi.la libkcmapi.la libkcutil.la libkcssl.la \
	libkchl.la libkcarchiver.la libkcarchivercore.la \
	${PROG_LIBS} ${SSL_LIBS} ${PYTHON_LIBS} ${XML2_LIBS} ${icu_uc_LIBS}


#
# kopano-fsck
#
kopano_fsck_SOURCES = \
	ECtools/fsck/fsck.cpp ECtools/fsck/fsck-main.cpp ECtools/fsck/fsck-calendar.cpp \
	ECtools/fsck/fsck-contact.cpp ECtools/fsck/fsck-task.cpp ECtools/fsck/fsck.h
kopano_fsck_LDADD = libmapi.la libkcmapi.la libkcutil.la ${PROG_LIBS}


#
# kopano-gateway
#
kopano_gateway_SOURCES = \
	gateway/ClientProto.h gateway/Gateway.cpp \
	gateway/IMAP.cpp gateway/IMAP.h \
	gateway/POP3.cpp gateway/POP3.h
kopano_gateway_LDADD = \
	libkcinetmapi.la libmapi.la libkcmapi.la libkcutil.la libkcssl.la \
	${PROG_LIBS} ${CRYPTO_LIBS} ${SSL_LIBS} ${XML2_LIBS} ${icu_uc_LIBS}


#
# kopano-ical
#
kopano_ical_SOURCES = \
	caldav/CalDAV.cpp caldav/CalDavProto.cpp caldav/CalDavProto.h \
	caldav/CalDavUtil.cpp caldav/CalDavUtil.h \
	caldav/Http.cpp caldav/Http.h \
	caldav/ProtocolBase.cpp caldav/ProtocolBase.h \
	caldav/WebDav.cpp caldav/WebDav.h caldav/iCal.cpp caldav/iCal.h
kopano_ical_LDADD = \
	libmapi.la libkcmapi.la libkcutil.la libkcicalmapi.la libkcssl.la \
	libkcfreebusy.la ${PROG_LIBS} ${SSL_LIBS} ${ICONV_LIBS} ${XML2_LIBS} \
	${ICAL_LIBS} ${icu_uc_LIBS}


#
# kopano-monitor
#
kopano_monitor_SOURCES = \
	ECtools/monitor/ECMonitorDefs.h \
	ECtools/monitor/ECQuotaMonitor.cpp ECtools/monitor/ECQuotaMonitor.h \
	ECtools/monitor/monitor.cpp
kopano_monitor_LDADD = libmapi.la libkcmapi.la libkcutil.la ${PROG_LIBS}


#
# kopano-passwd
#
kopano_passwd_SOURCES = ECtools/passwd/passwd.cpp
kopano_passwd_LDADD = libmapi.la libkcmapi.la libkcutil.la ${PROG_LIBS}


#
# kopano-spooler
#
kopano_spooler_CPPFLAGS = ${kopano_dagent_CPPFLAGS}
kopano_spooler_SOURCES = \
	spooler/PyMapiPlugin.cpp spooler/PyMapiPlugin.h \
	spooler/PythonSWIGRuntime.h spooler/Spooler.cpp \
	spooler/archive.cpp spooler/archive.h \
	spooler/mailer.cpp spooler/mailer.h spooler/spmain.h
kopano_spooler_LDADD = \
	libkcinetmapi.la libmapi.la libkcmapi.la libkcutil.la libkcssl.la \
	libkcarchiver.la libkcarchivercore.la \
	${PROG_LIBS} ${PYTHON_LIBS} ${XML2_LIBS}


#
# kopano-stats
#
kopano_stats_SOURCES = ECtools/stats/stats.cpp
kopano_stats_CPPFLAGS = ${AM_CPPFLAGS} ${NCURSES_FLAGS}
# ncurses needs to come first, because ICU_LIBS erroneously has -L/usr/lib64
# which overrides the ncurses lib selection >:-(
kopano_stats_LDADD = \
	${NCURSES_LIBS} libmapi.la libkcmapi.la libkcutil.la ${PROG_LIBS}


#
# kopano-server
#
kopano_server_SOURCES = \
	provider/server/ECServer.cpp \
	provider/server/ECSoapServerConnection.cpp provider/server/ECSoapServerConnection.h \
	provider/server/ECClientUpdate.cpp provider/server/ECClientUpdate.h \
	provider/server/ECThreadManager.cpp provider/server/ECThreadManager.h
kopano_server_LDADD = \
	libkcserver.la ${GSOAP_LIBS} libkcsoapserver.la libkccommon.la \
	libkcutil.la libkcmapi.la libkcssl.la \
	${PROG_LIBS} ${BOOST_FILESYSTEM_LIBS} ${BOOST_SYSTEM_LIBS} \
	${icu_uc_LIBS} ${icu_i18n_LIBS} ${DL_LIBS} \
	${MYSQL_LIBS} ${SSL_LIBS} ${PAM_LIBS} ${KRB5_LIBS} -lrt
# -E (-rdynamic) still needed because libkcserver.so references
# "searchfolder_restart_required", "_Z23kopano_get_server_statsPjPdS_S_"
# and "_Z18kopano_notify_doneP4soap"
kopano_server_LDFLAGS = ${AM_LDFLAGS} -Wl,-E ${BOOST_FILESYSTEM_LDFLAGS}


#
# libkcarchiver
#
libkcarchiver_la_SOURCES = \
	ECtools/archiver/ArchiverSession.cpp ECtools/archiver/ArchiverSession.h \
	ECtools/archiver/ArchiverSessionPtr.h \
	ECtools/archiver/archiver-common.cpp \
	ECtools/archiver/ArchiveManageImpl.cpp ECtools/archiver/ArchiveManageImpl.h \
	ECtools/archiver/ArchiveManage.h \
	ECtools/archiver/ArchiveStateCollector.cpp ECtools/archiver/ArchiveStateCollector.h \
	ECtools/archiver/ArchiveStateUpdater.cpp ECtools/archiver/ArchiveStateUpdater.h \
	ECtools/archiver/archivestateupdater_fwd.h \
	ECtools/archiver/helpers/ArchiveHelper.cpp ECtools/archiver/helpers/ArchiveHelper.h \
	ECtools/archiver/helpers/StoreHelper.cpp ECtools/archiver/helpers/StoreHelper.h \
	ECtools/archiver/helpers/MAPIPropHelper.cpp ECtools/archiver/helpers/MAPIPropHelper.h \
	ECtools/archiver/ECArchiverLogger.cpp ECtools/archiver/ECArchiverLogger.h
libkcarchiver_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	-Wl,--version-script=${default_sym}
libkcarchiver_la_LIBADD = libmapi.la libkcmapi.la libkcutil.la
EXTRA_libkcarchiver_la_DEPENDENCIES = ${default_sym}


#
# libkcarchivercore
#
libkcarchivercore_la_SOURCES = \
	ECtools/archiver/Archiver.cpp ECtools/archiver/Archiver.h \
	ECtools/archiver/AutoMAPI.h \
	ECtools/archiver/ArchiverImpl.cpp ECtools/archiver/ArchiverImpl.h \
	ECtools/archiver/operations/instanceidmapper.cpp \
	ECtools/archiver/operations/instanceidmapper.h \
	ECtools/archiver/operations/instanceidmapper_fwd.h \
	ECtools/archiver/operations/operations.cpp \
	ECtools/archiver/operations/operations.h \
	ECtools/archiver/operations/operations_fwd.h \
	ECtools/archiver/operations/copier.cpp \
	ECtools/archiver/operations/copier.h \
	ECtools/archiver/operations/deleter.cpp \
	ECtools/archiver/operations/deleter.h \
	ECtools/archiver/operations/stubber.cpp \
	ECtools/archiver/operations/stubber.h \
	ECtools/archiver/operations/transaction.cpp \
	ECtools/archiver/operations/transaction.h \
	ECtools/archiver/operations/transaction_fwd.h \
	ECtools/archiver/operations/postsaveaction.h \
	ECtools/archiver/operations/postsaveiidupdater.cpp \
	ECtools/archiver/operations/postsaveiidupdater.h \
	ECtools/archiver/ArchiveControlImpl.cpp ECtools/archiver/ArchiveControlImpl.h \
	ECtools/archiver/arc_database.cpp ECtools/archiver/arc_database.hpp \
	ECtools/archiver/arc_mysql.cpp ECtools/archiver/arc_mysql.hpp
libkcarchivercore_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	-Wl,--version-script=${default_sym}
libkcarchivercore_la_LIBADD = \
	libmapi.la libkcmapi.la libkcarchiver.la \
	${PROG_LIBS} ${SSL_LIBS} ${MYSQL_LIBS}
EXTRA_libkcarchivercore_la_DEPENDENCIES = ${default_sym}


#
# libkcclient
#
libkcclient_la_SOURCES = \
	provider/client/ClientUtil.cpp provider/client/ClientUtil.h \
	provider/client/DLLGlobal.h \
	provider/client/ECABContainer.cpp provider/client/ECABContainer.h \
	provider/client/ECABLogon.cpp provider/client/ECABLogon.h \
	provider/client/ECABProp.cpp provider/client/ECABProp.h \
	provider/client/ECABProvider.cpp provider/client/ECABProvider.h \
	provider/client/ECABProviderOffline.cpp provider/client/ECABProviderOffline.h \
	provider/client/ECABProviderSwitch.cpp provider/client/ECABProviderSwitch.h \
	provider/client/ECArchiveAwareAttach.cpp provider/client/ECArchiveAwareAttach.h \
	provider/client/ECArchiveAwareMessage.cpp provider/client/ECArchiveAwareMessage.h \
	provider/client/ECArchiveAwareMsgStore.cpp provider/client/ECArchiveAwareMsgStore.h \
	provider/client/ECAttach.cpp provider/client/ECAttach.h \
	provider/client/ECChangeAdvisor.cpp provider/client/ECChangeAdvisor.h \
	provider/client/ECDisplayTable.cpp provider/client/ECDisplayTable.h \
	provider/client/ECDistList.cpp provider/client/ECDistList.h \
	provider/client/ECExchangeExportChanges.cpp provider/client/ECExchangeExportChanges.h \
	provider/client/ECExchangeImportContentsChanges.cpp \
	provider/client/ECExchangeImportContentsChanges.h \
	provider/client/ECExchangeImportHierarchyChanges.cpp \
	provider/client/ECExchangeImportHierarchyChanges.h \
	provider/client/ECExchangeModifyTable.cpp provider/client/ECExchangeModifyTable.h \
	provider/client/ECExportAddressbookChanges.cpp \
	provider/client/ECExportAddressbookChanges.h \
	provider/client/ECGenericProp.cpp provider/client/ECGenericProp.h \
	provider/client/ics_client.hpp \
	provider/client/ECMAPIContainer.cpp provider/client/ECMAPIContainer.h \
	provider/client/ECMAPIFolder.cpp provider/client/ECMAPIFolder.h \
	provider/client/ECMAPIFolderPublic.cpp provider/client/ECMAPIFolderPublic.h \
	provider/client/ECMAPIProp.cpp provider/client/ECMAPIProp.h \
	provider/client/ECMAPITable.cpp provider/client/ECMAPITable.h \
	provider/client/ECMSProvider.cpp provider/client/ECMSProvider.h \
	provider/client/ECMSProviderOffline.cpp provider/client/ECMSProviderOffline.h \
	provider/client/ECMSProviderSwitch.cpp provider/client/ECMSProviderSwitch.h \
	provider/client/ECMailUser.cpp provider/client/ECMailUser.h \
	provider/client/ECMemTablePublic.cpp provider/client/ECMemTablePublic.h \
	provider/client/ECMessage.cpp provider/client/ECMessage.h \
	provider/client/ECMessageStreamImporterIStreamAdapter.cpp \
	provider/client/ECMessageStreamImporterIStreamAdapter.h \
	provider/client/ECMsgStore.cpp provider/client/ECMsgStore.h \
	provider/client/ECMsgStorePublic.cpp provider/client/ECMsgStorePublic.h \
	provider/client/ECNamedProp.cpp provider/client/ECNamedProp.h \
	provider/client/ECNotifyClient.cpp provider/client/ECNotifyClient.h \
	provider/client/ECNotifyMaster.cpp provider/client/ECNotifyMaster.h \
	provider/client/ECParentStorage.cpp provider/client/ECParentStorage.h \
	provider/client/ECPropertyEntry.cpp provider/client/ECPropertyEntry.h \
	provider/client/ECSessionGroupManager.cpp provider/client/ECSessionGroupManager.h \
	provider/client/ECXPLogon.cpp provider/client/ECXPLogon.h \
	provider/client/ECXPProvider.cpp provider/client/ECXPProvider.h \
	provider/client/EntryPoint.h \
	provider/client/IECPropStorage.h \
	provider/client/Mem.cpp provider/client/Mem.h \
	provider/client/ProviderUtil.cpp provider/client/ProviderUtil.h \
	provider/client/SessionGroupData.cpp provider/client/SessionGroupData.h \
	provider/client/WSABPropStorage.cpp provider/client/WSABPropStorage.h \
	provider/client/WSABTableView.cpp provider/client/WSABTableView.h \
	provider/client/WSMAPIFolderOps.cpp provider/client/WSMAPIFolderOps.h \
	provider/client/WSMAPIPropStorage.cpp provider/client/WSMAPIPropStorage.h \
	provider/client/WSMessageStreamExporter.cpp provider/client/WSMessageStreamExporter.h \
	provider/client/WSMessageStreamImporter.cpp provider/client/WSMessageStreamImporter.h \
	provider/client/WSSerializedMessage.cpp provider/client/WSSerializedMessage.h \
	provider/client/WSStoreTableView.cpp provider/client/WSStoreTableView.h \
	provider/client/WSTableView.cpp provider/client/WSTableView.h \
	provider/client/WSTransport.cpp provider/client/WSTransport.h \
	provider/client/WSUtil.cpp provider/client/WSUtil.h \
	provider/client/epclient.cpp \
	provider/client/resource.h
libkcclient_la_LIBADD = \
	libkcmapi.la libkcssl.la libkcutil.la libkcfreebusy.la libkcsync.la \
	libmapi.la libkccommon.la libkcsoapclient.la -lstdc++ \
	${GSOAP_LIBS} ${ICU_LIBS} ${SSL_LIBS} ${UUID_LIBS}
libkcclient_la_LDFLAGS = ${AM_LDFLAGS} -avoid-version -module


#
# libkccommon
#
libkccommon_la_SOURCES = \
	provider/common/ECLicenseClient.h provider/common/ECLicenseClient.cpp \
	provider/common/ECSearchClient.h provider/common/ECSearchClient.cpp \
	provider/common/SOAPAlloc.h provider/common/SOAPDebug.cpp provider/common/SOAPDebug.h \
	provider/common/SOAPHelpers.cpp provider/common/SOAPHelpers.h \
	provider/common/SOAPSock.cpp provider/common/SOAPSock.h \
	provider/common/SOAPUtils.cpp provider/common/SOAPUtils.h \
	provider/common/pcuser.cpp provider/common/pcutil.cpp provider/common/pcutil.hpp \
	provider/common/soapctor.cpp provider/common/versions.h
libkccommon_la_LIBADD = \
	libkcutil.la libkcmapi.la libkcsoapclient.la libkcssl.la \
	${GSOAP_LIBS} ${SSL_LIBS}
libkccommon_la_LDFLAGS = ${AM_LDFLAGS} \
	-Wl,--version-script=${default_sym}
EXTRA_libkccommon_la_DEPENDENCIES = ${default_sym}


#
# libkccontacts
#
libkccontacts_la_SOURCES = \
	provider/contacts/epcontact.cpp provider/contacts/ZCABData.h \
	provider/contacts/ZCABContainer.h provider/contacts/ZCABContainer.cpp \
	provider/contacts/ZCABLogon.h provider/contacts/ZCABLogon.cpp \
	provider/contacts/ZCABProvider.h provider/contacts/ZCABProvider.cpp \
	provider/contacts/ZCMAPIProp.h provider/contacts/ZCMAPIProp.cpp
libkccontacts_la_LDFLAGS = ${AM_LDFLAGS} -avoid-version -module
libkccontacts_la_LIBADD = \
	libkcutil.la libkcmapi.la libmapi.la -lstdc++ ${UUID_LIBS} ${ICU_LIBS}


#
# libkcfreebusy
#
libkcfreebusydir = ${includedir}/libfreebusy
libkcfreebusy_HEADERS = \
	libfreebusy/ECEnumFBBlock.h libfreebusy/ECFBBlockList.h \
	libfreebusy/ECFreeBusyData.h libfreebusy/ECFreeBusySupport.h \
	libfreebusy/ECFreeBusyUpdate.h libfreebusy/PublishFreeBusy.h \
	libfreebusy/freebusy.h libfreebusy/freebusyguid.h \
	libfreebusy/freebusytags.h libfreebusy/freebusyutil.h \
	libfreebusy/recurrence.h
libkcfreebusy_la_SOURCES = \
	libfreebusy/ECEnumFBBlock.cpp libfreebusy/ECFBBlockList.cpp \
	libfreebusy/ECFreeBusyData.cpp libfreebusy/ECFreeBusySupport.cpp \
	libfreebusy/ECFreeBusyUpdate.cpp libfreebusy/PublishFreeBusy.cpp \
	libfreebusy/freebusyutil.cpp libfreebusy/recurrence.cpp
libkcfreebusy_la_LIBADD = \
	libkcutil.la libkcmapi.la libmapi.la
libkcfreebusy_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	-Wl,--version-script=${default_sym}
EXTRA_libkcfreebusy_la_DEPENDENCIES = ${default_sym}


#
# libkchl
#
libkchl_la_SOURCES = mapi4linux/src/hl.cpp
libkchl_la_LIBADD = libmapi.la
libkchl_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined -version-info 0:0:0 \
	-Wl,--version-script=${default_sym}
EXTRA_libkchl_la_DEPENDENCIES = ${default_sym}


#
# libkcicalmapi
#
libkcicalmapidir = ${includedir}/icalmapi
libkcicalmapi_HEADERS = \
	libicalmapi/ICalToMAPI.h libicalmapi/MAPIToICal.h \
	libicalmapi/icalitem.h libicalmapi/icalmapi.h \
	libicalmapi/icalrecurrence.h libicalmapi/icaluid.h \
	libicalmapi/nameids.h libicalmapi/valarm.h libicalmapi/vconverter.h \
	libicalmapi/vevent.h libicalmapi/vfreebusy.h libicalmapi/vtimezone.h \
	libicalmapi/vtodo.h
libkcicalmapi_la_SOURCES = \
	libicalmapi/ICalToMAPI.cpp libicalmapi/MAPIToICal.cpp \
	libicalmapi/icalinit.cpp libicalmapi/icalrecurrence.cpp \
	libicalmapi/icaluid.cpp libicalmapi/nameids.cpp \
	libicalmapi/valarm.cpp libicalmapi/vconverter.cpp \
	libicalmapi/vevent.cpp libicalmapi/vfreebusy.cpp \
	libicalmapi/vtimezone.cpp libicalmapi/vtodo.cpp
libkcicalmapi_la_LIBADD = \
	libkcfreebusy.la libmapi.la libkcmapi.la libkcutil.la ${ICAL_LIBS}
libkcicalmapi_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined -version-info 0:0:0 \
	-Wl,--version-script=${default_sym}
EXTRA_libkcicalmapi_la_DEPENDENCIES = ${default_sym}


#
# libkcinetmapi
#
libkcinetmapi_la_SOURCES = \
	inetmapi/ECMapiUtils.cpp inetmapi/ECMapiUtils.h \
	inetmapi/ECVMIMEUtils.cpp inetmapi/ECVMIMEUtils.h \
	inetmapi/MAPISMTPTransport.cpp inetmapi/MAPISMTPTransport.h \
	inetmapi/MAPIToVMIME.cpp inetmapi/MAPIToVMIME.h \
	inetmapi/SMIMEMessage.cpp inetmapi/SMIMEMessage.h \
	inetmapi/VMIMEToMAPI.cpp inetmapi/VMIMEToMAPI.h \
	inetmapi/inetmapi.cpp \
	inetmapi/inputStreamMAPIAdapter.cpp \
	inetmapi/inputStreamMAPIAdapter.h \
	inetmapi/mapiAttachment.cpp inetmapi/mapiAttachment.h \
	inetmapi/mapiTextPart.cpp inetmapi/mapiTextPart.h \
	inetmapi/outputStreamMAPIAdapter.cpp \
	inetmapi/outputStreamMAPIAdapter.h \
	inetmapi/serviceRegistration.inl \
	inetmapi/tnef.cpp inetmapi/tnef.h
# Add '-z now' -> -Wl,-z, -Wl,now to LD flags so that we do early binding of
# symbols. This fixes a problem in which libvmime attempts to resolve a symbol
# in libinetmapi.so during the unload of vmime, initiated by the unload of
# libinetmapi. On some systems, this fails to resolve the symbol during unload,
# resulting in a crash in __cxa_finalize during unload of mapi.so
libkcinetmapi_la_LIBADD = \
	libmapi.la libkcmapi.la libkcutil.la libkcicalmapi.la \
	${VMIME_LIBS} ${XML2_LIBS}
libkcinetmapi_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	-version-info 0:0:0 -Wl,-z -Wl,now -Wl,--version-script=${default_sym}
EXTRA_libkcinetmapi_la_DEPENDENCIES = ${default_sym}


#
# libkcserver
#
libkcserver_la_SOURCES = \
	provider/libserver/ECABObjectTable.cpp provider/libserver/ECABObjectTable.h \
	provider/libserver/ECAttachmentStorage.cpp provider/libserver/ECAttachmentStorage.h \
	provider/libserver/ECCacheManager.cpp provider/libserver/ECCacheManager.h \
	provider/libserver/ECConvenientDepthABObjectTable.cpp \
	provider/libserver/ECConvenientDepthABObjectTable.h \
	provider/libserver/ECConvenientDepthObjectTable.cpp \
	provider/libserver/ECConvenientDepthObjectTable.h \
	provider/libserver/ECConversion.cpp provider/libserver/ECConversion.h \
	provider/libserver/ECDBDef.h \
	provider/libserver/ECDatabase.h \
	provider/libserver/ECDatabaseFactory.cpp provider/libserver/ECDatabaseFactory.h \
	provider/libserver/ECDatabaseMySQL.cpp provider/libserver/ECDatabaseMySQL.h \
	provider/libserver/ECDatabaseUpdate.cpp provider/libserver/ECDatabaseUpdate.h \
	provider/libserver/ECDatabaseUtils.cpp provider/libserver/ECDatabaseUtils.h \
	provider/libserver/ECGenProps.cpp provider/libserver/ECGenProps.h \
	provider/libserver/ECGenericObjectTable.cpp \
	provider/libserver/ECGenericObjectTable.h \
	provider/libserver/ECICS.cpp provider/libserver/ECICS.h \
	provider/libserver/ECICSHelpers.cpp provider/libserver/ECICSHelpers.h \
	provider/libserver/ECIndexer.cpp provider/libserver/ECIndexer.h \
	provider/libserver/ECKrbAuth.cpp provider/libserver/ECKrbAuth.h \
	provider/libserver/ECLockManager.cpp provider/libserver/ECLockManager.h \
	provider/libserver/ECMAPI.h \
	provider/libserver/ECMailBoxTable.cpp provider/libserver/ECMailBoxTable.h \
	provider/libserver/ECMultiStoreTable.cpp provider/libserver/ECMultiStoreTable.h \
	provider/libserver/ECNotification.cpp provider/libserver/ECNotification.h \
	provider/libserver/ECNotificationManager.cpp provider/libserver/ECNotificationManager.h \
	provider/libserver/ECPamAuth.cpp provider/libserver/ECPamAuth.h \
	provider/libserver/ECPluginFactory.cpp provider/libserver/ECPluginFactory.h \
	provider/libserver/ECPluginSharedData.cpp \
	provider/libserver/ECS3Attachment.cpp provider/libserver/ECS3Attachment.h \
	provider/libserver/ECSearchFolders.cpp provider/libserver/ECSearchFolders.h \
	provider/libserver/ECSearchObjectTable.cpp provider/libserver/ECSearchObjectTable.h \
	provider/libserver/ECSecurity.cpp provider/libserver/ECSecurity.h \
	provider/libserver/ECSecurityOffline.cpp provider/libserver/ECSecurityOffline.h \
	provider/libserver/ECServerEntrypoint.cpp provider/libserver/ECServerEntrypoint.h \
	provider/libserver/ECSession.cpp provider/libserver/ECSession.h \
	provider/libserver/ECSessionGroup.cpp provider/libserver/ECSessionGroup.h \
	provider/libserver/ECSessionManager.cpp provider/libserver/ECSessionManager.h \
	provider/libserver/ECSessionManagerOffline.cpp \
	provider/libserver/ECSessionManagerOffline.h \
	provider/libserver/ECStatsCollector.cpp provider/libserver/ECStatsCollector.h \
	provider/libserver/ECStatsTables.cpp provider/libserver/ECStatsTables.h \
	provider/libserver/ECStoreObjectTable.cpp provider/libserver/ECStoreObjectTable.h \
	provider/libserver/ECStringCompat.cpp provider/libserver/ECStringCompat.h \
	provider/libserver/ECSubRestriction.cpp provider/libserver/ECSubRestriction.h \
	provider/libserver/ECTPropsPurge.cpp provider/libserver/ECTPropsPurge.h \
	provider/libserver/ECTableManager.cpp provider/libserver/ECTableManager.h \
	provider/libserver/ECTestProtocol.cpp provider/libserver/ECTestProtocol.h \
	provider/libserver/ECUserManagement.cpp provider/libserver/ECUserManagement.h \
	provider/libserver/ECUserManagementOffline.cpp \
	provider/libserver/ECUserManagementOffline.h \
	provider/libserver/ECUserStoreTable.cpp provider/libserver/ECUserStoreTable.h \
	provider/libserver/StorageUtil.cpp provider/libserver/StorageUtil.h \
	provider/libserver/StreamUtil.cpp provider/libserver/StreamUtil.h \
	provider/libserver/cmd.cpp provider/libserver/cmd.hpp \
	provider/libserver/cmdutil.cpp provider/libserver/cmdutil.hpp \
	provider/libserver/logontime.cpp provider/libserver/logontime.hpp
libkcserver_la_LIBADD = \
	libkcutil.la libkccommon.la \
	-lpthread ${icu_i18n_LIBS} ${icu_uc_LIBS} \
	${KRB5_LIBS} ${MYSQL_LIBS} ${PAM_LIBS} ${SSL_LIBS}
libkcserver_la_LDFLAGS = ${AM_LDFLAGS} \
	-Wl,--version-script=${default_sym}
EXTRA_libkcserver_la_DEPENDENCIES = ${default_sym}


#
# libkcmapi
#
noinst_HEADERS += \
	common/ECMemStream.h common/HtmlEntity.h common/HtmlToTextParser.h \
	common/IStreamAdapter.h common/rtfutil.h
libkcmapi_la_SOURCES = \
	common/ECABEntryID.cpp common/ECDebug.cpp common/ECDebugPrint.cpp \
	common/ECGuid.cpp common/ECKeyTable.cpp common/ECMemStream.cpp \
	common/ECUnknown.cpp common/HtmlEntity.cpp common/HtmlToTextParser.cpp \
	common/IStreamAdapter.cpp common/MAPIErrors.cpp common/kcodes.cpp \
	common/codepage.cpp common/rtfutil.cpp
libkcmapi_la_LIBADD = libkcutil.la -lpthread
libkcmapi_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	-Wl,--version-script=${default_sym}
EXTRA_libkcmapi_la_DEPENDENCIES = ${default_sym}


#
# libkcsoapclient
#
nodist_libkcsoapclient_la_SOURCES = provider/soap/soapC.cpp provider/soap/soapClient.cpp
libkcsoapclient_la_LIBADD = ${GSOAP_LIBS}
libkcsoapclient_la_LDFLAGS = ${AM_LDFLAGS} \
	-Wl,--version-script=${default_sym}
EXTRA_libkcsoapclient_la_DEPENDENCIES = ${default_sym}


#
# libkcsoapserver
#
nodist_libkcsoapserver_la_SOURCES = provider/soap/soapC.cpp provider/soap/soapServer.cpp
libkcsoapserver_la_LIBADD = ${GSOAP_LIBS}
libkcsoapserver_la_LDFLAGS = ${AM_LDFLAGS} \
	-Wl,--version-script=${default_sym}
EXTRA_libkcsoapserver_la_DEPENDENCIES = ${default_sym}


#
# libkcssl
#
noinst_HEADERS += common/ECChannelClient.h common/SSLUtil.h
libkcssl_la_SOURCES = \
	common/ECChannel.cpp common/ECChannelClient.cpp common/SSLUtil.cpp
libkcssl_la_LIBADD = libkcutil.la -lpthread ${SSL_LIBS}
libkcssl_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	-Wl,--version-script=${default_sym}
EXTRA_libkcssl_la_DEPENDENCIES = ${default_sym}


#
# libkcsync
#
libkcsyncdir = ${includedir}/libkcsync
libkcsync_HEADERS = \
	libsync/ECLibSync.h libsync/ECOfflineABImporter.h \
	libsync/ECResyncSet.h libsync/ECSyncContext.h libsync/ECSyncLog.h \
	libsync/ECSyncSettings.h libsync/ECSyncUtil.h
libkcsync_la_SOURCES = \
	libsync/ECOfflineABImporter.cpp libsync/ECResyncSet.cpp \
	libsync/ECSyncContext.cpp libsync/ECSyncLog.cpp \
	libsync/ECSyncSettings.cpp libsync/ECSyncUtil.cpp
libkcsync_la_LIBADD = libkcmapi.la libmapi.la
libkcsync_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	-Wl,--version-script=${default_sym}
EXTRA_libkcsync_la_DEPENDENCIES = ${default_sym}


#
# libkcutil
#
noinst_HEADERS += \
	common/ConsoleTable.h common/ECACL.h common/ECCache.h common/ECConfigImpl.h \
	common/ECFifoBuffer.h common/ECSerializer.h common/StatsClient.h \
	common/SymmetricCrypt.h common/TimeUtil.h common/TmpPath.h common/fileutil.h \
	common/utf8.h common/charset/utf32string.h \
	common/charset/localeutil.h common/ustringutil/utfutil.h \
	common/utf8/checked.h common/utf8/core.h common/utf8/unchecked.h
libkcutil_la_SOURCES = \
	common/ConsoleTable.cpp common/ECACL.cpp common/ECCache.cpp \
	common/ECConfig.cpp common/ECConfigImpl.cpp common/ECFifoBuffer.cpp \
	common/ECGetText.cpp common/ECIConv.cpp common/ECLogger.cpp \
	common/ECScheduler.cpp common/ECSerializer.cpp common/ECThreadPool.cpp \
	common/StatsClient.cpp common/SymmetricCrypt.cpp common/TimeUtil.cpp \
	common/TmpPath.cpp common/Trace.cpp common/UnixUtil.cpp common/base64.cpp \
	common/fileutil.cpp common/md5.cpp common/my_getopt.cpp common/platform.cpp \
	common/platform.linux.cpp common/stringutil.cpp \
	common/ustringutil.cpp common/charset/convert.cpp \
	common/charset/convstring.cpp common/charset/traits.cpp \
	common/charset/localeutil.cpp
libkcutil_la_LIBADD = \
	-lpthread -lz ${BOOST_FILESYSTEM_LIBS} ${CRYPTO_LIBS} ${UUID_LIBS}
libkcutil_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	-Wl,--version-script=${default_sym}
EXTRA_libkcutil_la_DEPENDENCIES = ${default_sym}
if HAVE_ICU
libkcutil_la_SOURCES += common/ustringutil/utfutil.cpp
libkcutil_la_LIBADD += ${icu_uc_LIBS} ${icu_i18n_LIBS}
endif


#
# libmapi
#
libmapi_la_SOURCES = \
	mapi4linux/src/common.cpp mapi4linux/src/mapidefs.cpp mapi4linux/src/mapispi.cpp mapi4linux/src/mapiutil.cpp \
	mapi4linux/src/mapix.cpp mapi4linux/src/rtf.cpp mapi4linux/src/imessage.cpp mapi4linux/src/m4l.common.h \
	mapi4linux/src/m4l.mapidefs.h mapi4linux/src/m4l.mapispi.h mapi4linux/src/m4l.mapiutil.h \
	mapi4linux/src/m4l.mapix.h mapi4linux/src/mapi4linux.h mapi4linux/src/m4l.debug.h mapi4linux/src/rtf.h \
	mapi4linux/src/m4l.mapisvc.cpp mapi4linux/src/m4l.mapisvc.h
libmapi_la_LIBADD = \
	libkcutil.la libkcmapi.la libm4lcommon.la \
	${BOOST_FILESYSTEM_LIBS} ${BOOST_SYSTEM_LIBS} ${PROG_LIBS} ${DL_LIBS}
libmapi_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined -version-info 1:0:0 \
	-Wl,--version-script=mapi4linux/src/libmapi.sym ${BOOST_FILESYSTEM_LDFLAGS}
EXTRA_libmapi_la_DEPENDENCIES = mapi4linux/src/libmapi.sym

mapi4linux/src/libmapi.sym: ${top_builddir}/config.status


#
# libm4lcommon
#
libm4lcommondir = ${includedir}/mapi4linux
libm4lcommon_HEADERS = m4lcommon/include/edkguid.h
# These were split off libcommon_mapi
libm4lcommon_la_SOURCES = \
	m4lcommon/CommonUtil.cpp \
	m4lcommon/ECFeatureList.h m4lcommon/ECInvariantChecker.h \
	m4lcommon/ECIterators.cpp m4lcommon/ECIterators.h \
	m4lcommon/ECMAPIDebug.cpp m4lcommon/ECMAPIDebug.h \
	m4lcommon/ECMemTable.cpp m4lcommon/HrException.h \
	m4lcommon/IECChangeAdviseSink.h \
	m4lcommon/IECChangeAdvisor.h \
	m4lcommon/IECExchangeModifyTable.h \
	m4lcommon/IECExportAddressbookChanges.h \
	m4lcommon/IECExportChanges.h \
	m4lcommon/IECImportAddressbookChanges.h \
	m4lcommon/IECImportContentsChanges.h \
	m4lcommon/IECImportHierarchyChanges.h \
	m4lcommon/IECMultiStoreTable.h m4lcommon/IECSpooler.h \
	m4lcommon/IECTestProtocol.h m4lcommon/IMAPIOffline.h \
	m4lcommon/MAPIConsoleTable.cpp m4lcommon/MAPIConsoleTable.h \
	m4lcommon/MAPINotifSink.cpp m4lcommon/MAPINotifSink.h \
	m4lcommon/Util.cpp \
	m4lcommon/favoritesutil.cpp m4lcommon/favoritesutil.h \
	m4lcommon/mapicontact.h
# These were split off the old libcommon_util
noinst_HEADERS += \
	m4lcommon/ECFeatures.h
libm4lcommon_la_SOURCES += \
	m4lcommon/ECFeatures.cpp m4lcommon/ECRestriction.cpp \
	m4lcommon/RecurrenceState.cpp m4lcommon/userutil.cpp
libm4lcommon_la_LIBADD = \
	libkcutil.la libkcmapi.la -lpthread


#
# test programs
#
cov_scope_SOURCES = common/cov-scope.cpp
mapitime_SOURCES = ECtools/mapitime.cpp
mapitime_LDADD = -lrt libmapi.la libkcmapi.la libkcutil.la ${icu_uc_LIBS}


#
# spooler generators
#
BUILT_SOURCES += spooler/PythonSWIGRuntime.h
CLEANFILES += spooler/PythonSWIGRuntime.h
EXTRA_DIST += spooler/PythonSWIGRuntime.h

spooler/PythonSWIGRuntime.h:
	${SWIG_EXEC} -python -external-runtime $@


#
# soap generators
#
SOAPHEADER = provider/include/proto.h
SOAP_NS = provider/soap/KCmd.nsmap provider/soap/KCmd.wsdl
SOAP_H_FILES = provider/soap/soapH.h provider/soap/soapStub.h
SOAP_CPP_CLIENT = provider/soap/soapClient.cpp provider/soap/soapC.cpp
SOAP_LIB_FILES = provider/soap/soapClientLib.cpp provider/soap/soapServerLib.cpp \
	provider/soap/soapKCmdObject.h
SOAP_CPP_SRC = ${SOAP_CPP_CLIENT} provider/soap/soapServer.cpp ${SOAP_H_FILES} \
	${SOAP_NS} ${SOAP_LIB_FILES}

${SOAP_CPP_SRC}: provider/soap/ns.xsd
provider/soap/ns.xsd: ${SOAPHEADER}
	${MKDIR_P} provider/soap
	soapcpp2 -z1 -x -Iinclude/ -d provider/soap/ $<
	if ! grep -q 'propVal\[\]' provider/soap/soapC*; then \
		echo "ERROR: This gsoap generator did not produce an acceptable wire protocol. http://sf.net/p/gsoap2/bugs/1075/"; \
		exit 1; \
	fi

BUILT_SOURCES += provider/soap/ns.xsd ${SOAP_CPP_SRC}
CLEANFILES += ${SOAP_CPP_SRC} provider/soap/*


#
# server userdb plugins
#
COMMON_PLUGIN_FILES = provider/plugins/plugin.h
COMMON_DBPLUGIN_FILES = provider/plugins/DBBase.h provider/plugins/DBBase.cpp
pkglib_LTLIBRARIES += dbplugin.la
#noinst_DATA += dbplugin.ldd
dbplugin_la_SOURCES = \
	provider/plugins/DBUserPlugin.cpp provider/plugins/DBUserPlugin.h \
	${COMMON_PLUGIN_FILES} ${COMMON_DBPLUGIN_FILES}
dbplugin_la_LDFLAGS = -module -avoid-version
dbplugin_la_LIBADD = \
	libkcutil.la libkcserver.la ${MYSQL_LIBS}

if WITH_UNIX
pkglib_LTLIBRARIES += unixplugin.la
#noinst_DATA += unixplugin.ldd
unixplugin_la_SOURCES = \
	provider/plugins/UnixUserPlugin.cpp provider/plugins/UnixUserPlugin.h \
	${COMMON_PLUGIN_FILES} ${COMMON_DBPLUGIN_FILES}
unixplugin_la_LDFLAGS = ${AM_LDFLAGS} -module -avoid-version
unixplugin_la_LIBADD = \
	libkcutil.la libkcserver.la ${MYSQL_LIBS} ${CRYPT_LIBS}
endif

if WITH_LDAP
pkglib_LTLIBRARIES += ldapplugin.la
#noinst_DATA += ldapplugin.ldd
noinst_DATA += .libs/ldapmsplugin.so
ldapplugin_la_SOURCES = \
	provider/plugins/LDAPUserPlugin.cpp provider/plugins/LDAPUserPlugin.h \
	provider/plugins/LDAPCache.cpp provider/plugins/LDAPCache.h \
	provider/plugins/ldappasswords.cpp provider/plugins/ldappasswords.h \
	${COMMON_PLUGIN_FILES}
ldapplugin_la_LDFLAGS = ${AM_LDFLAGS} -module -avoid-version
ldapplugin_la_LIBADD = \
	libkcutil.la \
	libkcserver.la ${CRYPTO_LIBS} ${LDAP_LIBS}

.libs/ldapmsplugin.so: ldapplugin.la
	${AM_V_GEN}ln -fs ldapplugin.so .libs/ldapmsplugin.so

install-exec-local:
	${MKDIR_P} ${DESTDIR}${pkglibdir}
	ln -fs ldapplugin.so ${DESTDIR}${pkglibdir}/ldapmsplugin.so
endif


#
# manual pages
#
genpages = \
	doc/kopano.7 \
	doc/kopano-admin.8 \
	doc/kopano-archiver.8 doc/kopano-archiver.cfg.5 \
	doc/kopano-autorespond.8 \
	doc/kopano-cfgchecker.8 \
	doc/kopano-dagent.8 doc/kopano-dagent.cfg.5 \
	doc/kopano-mr-accept.8 doc/kopano-mr-process.8 \
	doc/kopano-fsck.1 \
	doc/kopano-gateway.8 doc/kopano-gateway.cfg.5 \
	doc/kopano-ical.8 doc/kopano-ical.cfg.5 \
	doc/kopano-ldap.cfg.5 \
	doc/kopano-mailbox-permissions.8 \
	doc/kopano-monitor.8 doc/kopano-monitor.cfg.5 \
	doc/kopano-passwd.1 \
	doc/kopano-search.8 doc/kopano-search.cfg.5 \
	doc/kopano-server.8 doc/kopano-server.cfg.5 \
	doc/kopano-set-oof.1 \
	doc/kopano-spooler.8 doc/kopano-spooler.cfg.5 \
	doc/kopano-backup.8 doc/kopano-backup.cfg.5 \
	doc/kopano-stats.1 \
	doc/kopano-unix.cfg.5
dist_man_MANS = ${genpages}
CLEANFILES += ${genpages} doc/kopano.0
EXTRA_DIST += doc/manual.xml
${genpages}: doc/kopano.0

# Since the manual.xml file is in UTF-8, we need to call 'xmlto' in UTF-8 mode
# as well.
doc/kopano.0: ${top_srcdir}/doc/manual.xml
	@${AM_V_GEN} if [ -z "$(PROG_XMLTO)" ]; then \
		echo Please install xmlto to generate manual pages; \
	else \
		${MKDIR_P} doc; \
		LC_ALL=en_US.UTF-8 ${PROG_XMLTO} -o doc man ${top_srcdir}/doc/manual.xml && \
		touch $@; \
	fi


#
# build infrastructure
#
default.sym: ${top_builddir}/config.status
include global.am


#OLD TOPLEVEL FM
EXTRA_DIST += version versionheader.sh \
	LICENSE.txt AGPL-3 \
	lddcheck global.am pwdcheck.sh \
	common/include/kopano/ecversion.h

BUILT_SOURCES += source-moved-check common/include/kopano/ecversion.h
CLEANFILES += common/include/kopano/ecversion.h

dist_sbin_SCRIPTS += tools/python-scripts/kopano-cachestat.py
gdbautoloaddir = ${datadir}/gdb/auto-load/${libdir}
dist_gdbautoload_DATA = tools/libmapi.so.1.0.0-gdb.py

DISTCHECK_CONFIGURE_FLAGS = --with-dcprefix='$${prefix}'

.PHONY: source-moved-check
source-moved-check:
	@${top_srcdir}/pwdcheck.sh "${abs_top_builddir}"

common/include/kopano/ecversion.h: versionheader.sh
	${MKDIR_P} common/include/kopano
	sh ${top_srcdir}/versionheader.sh >$@

dist-hook:
	find $(distdir) -type d -name .svn -exec rm -rf {} \; 2>/dev/null || true

default.sym: ${top_builddir}/config.status
