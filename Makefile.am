# -*- Makefile -*-

ACLOCAL_AMFLAGS = -I autoconf

SUBDIRS = . ECtools/archiver/include/kopano \
	common/include/kopano m4lcommon/include/kopano \
	mapi4linux/include inetmapi/include/inetmapi provider/include \
	spooler/python
SUBDIRS += installer po
if ENABLE_PYTHON
SUBDIRS += ECtools/backup ECtools/cli ECtools/migration-pst \
	ECtools/presence ECtools/search swig
endif


AM_CPPFLAGS = ${ZCPPFLAGS} ${DEBUGFLAGS} -DLDAP_THREAD_SAFE \
	-DMAPICONFIGDIR=\"${prefix}/lib/mapi.d:${sysconfdir}/mapi.d\" -DPKGLIBDIR='"${pkglibdir}"' \
	-DSOAP_CMAC='__attribute__((visibility("default")))' \
	-DSOAP_FMAC3=SOAP_CMAC -DSOAP_FMAC5=SOAP_CMAC -DSOAP_NMAC=SOAP_CMAC \
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
	-I${top_srcdir}/librosie \
	-I${top_srcdir}/ECtools/archiver \
	${CRYPTO_CFLAGS} ${curl_CFLAGS} ${GSOAP_CFLAGS} ${ICAL_CFLAGS} \
	${KRB5_CFLAGS} ${LDAP_FLAGS} ${MYSQL_INCLUDES} ${SSL_CFLAGS} \
	${s3_CFLAGS} ${TCMALLOC_CFLAGS} ${tidy_CPPFLAGS} \
	${VMIME_CFLAGS} ${XML2_CFLAGS}
AM_CXXFLAGS = ${ZCXXFLAGS} -Wno-sign-compare


#
# target programs
#
bin_PROGRAMS = kopano-fsck kopano-passwd kopano-stats
dist_bin_SCRIPTS = ECtools/migration-imap/kopano-migration-imap \
	ECtools/utils/kopano-set-oof
sbin_PROGRAMS = \
	kopano-admin kopano-archiver kopano-dagent kopano-gateway kopano-ical \
	kopano-monitor kopano-server kopano-spooler
dist_sbin_SCRIPTS = ECtools/utils/kopano-mailbox-permissions \
	ECtools/archiver/scripts/kopano-archiver-aclset \
	ECtools/archiver/scripts/kopano-archiver-aclsync \
	ECtools/archiver/scripts/kopano-archiver-restore \
	tools/python-scripts/kopano-fix-ipm-subtree \
	tools/python-scripts/kopano-localize-folders \
	tools/python-scripts/kopano-recreate-systemfolders \
	tools/python-scripts/kopano-rules
noinst_PROGRAMS = kopano-cfgchecker
if HAVE_TIDY
noinst_PROGRAMS += rosie-test
endif
pkglibexec_PROGRAMS = mapitime


#
# target libraries
#
# `make install` ignores both *_LIBADD, but also calls libtool for relinking,
# so the order of lib_LTLIBRARIES is important! Dependant libs go to the right.
# (Order was previously implied by the SUBDIRS variable.)
#
if HAVE_TIDY
COND_libkcrosie_la = libkcrosie.la
COND_libkcrosie_ldd = libkcrosie.ldd
endif
lib_LTLIBRARIES = \
	libkcutil.la libkcmapi.la libkcssl.la libmapi.la libkchl.la \
	libkcfreebusy.la libkcsync.la libkcsoap.la libkcserver.la \
	libkcicalmapi.la ${COND_libkcrosie_la} libkcinetmapi.la \
	libkcarchiver.la libkcarchivercore.la
noinst_LTLIBRARIES =
if ENABLE_PYTHON
noinst_LTLIBRARIES += libkcpyplug.la
endif
noinst_DATA = \
	kopano-archiver.ldx kopano-cfgchecker.ldx kopano-dagent.ldx \
	kopano-gateway.ldx kopano-ical.ldx kopano-monitor.ldx \
	kopano-passwd.ldx kopano-server.ldx kopano-spooler.ldx \
	kopano-stats.ldx \
	libkcarchiver.ldd libkcarchivercore.ldd libkcclient.ldd \
	libkccontacts.ldd libkcfreebusy.ldd libkchl.ldd libkcicalmapi.ldd \
	libkcinetmapi.ldd libkcmapi.ldd ${COND_libkcrosie_ldd} \
	libkcserver.ldd libkcssl.ldd libkcsync.ldd libkcutil.ldd libmapi.ldd
# Undefined references
#noinst_DATA += libkcsoap.ldd
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
mapiinfdir = ${prefix}/lib/mapi.d
dist_mapiinf_DATA = provider/client/kopano.inf provider/contacts/zcontacts.inf
BUILT_SOURCES =
CLEANFILES = ${noinst_DATA}
EXTRA_DIST = autoconf/1126.h php-ext/mapi.sym
default_sym = ${top_builddir}/default.sym
default_SYFLAGS = -Wl,--version-script=${default_sym}
plugin_SYFLAGS = -Wl,--version-script=provider/plugin.sym


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
	ECtools/cfgchecker/ECConfigCheck.cpp ECtools/cfgchecker/ECConfigCheck.h \
	ECtools/cfgchecker/LDAPConfigCheck.cpp \
	ECtools/cfgchecker/ServerConfigCheck.cpp \
	ECtools/cfgchecker/cfg.cpp
kopano_cfgchecker_LDADD = libkcutil.la libkcmapi.la libmapi.la ${PROG_LIBS}


#
# kopano-dagent
#
libkcpyplug_la_CPPFLAGS = \
	${AM_CPPFLAGS} -Ispooler ${PYTHON_CFLAGS} -DSWIG_TYPE_TABLE=MAPI
libkcpyplug_la_SOURCES = \
	common/pymem.hpp spooler/PyMapiPlugin.cpp spooler/PyMapiPlugin.h \
	spooler/PythonSWIGRuntime.h
libkcpyplug_la_LIBADD = \
	libkcutil.la libmapi.la ${PYTHON_LIBS}

kopano_dagent_SOURCES = \
	spooler/DAgent.cpp spooler/LMTP.cpp spooler/LMTP.h \
	spooler/archive.cpp spooler/archive.h \
	spooler/rules.cpp spooler/rules.h
kopano_dagent_LDADD = \
	libkcinetmapi.la libmapi.la libkcmapi.la libkcutil.la libkcssl.la \
	libkchl.la libkcarchiver.la libkcarchivercore.la \
	${PROG_LIBS} ${SSL_LIBS} ${XML2_LIBS} ${icu_uc_LIBS}
if ENABLE_PYTHON
kopano_dagent_LDADD += libkcpyplug.la
endif


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
	libkchl.la -lpthread \
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
	libkcfreebusy.la -lpthread \
	${PROG_LIBS} ${SSL_LIBS} ${ICONV_LIBS} ${XML2_LIBS} \
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
kopano_spooler_SOURCES = \
	spooler/Spooler.cpp spooler/archive.cpp spooler/archive.h \
	spooler/mailer.cpp spooler/mailer.h
kopano_spooler_LDADD = \
	libkcinetmapi.la libmapi.la libkcmapi.la libkcutil.la libkcssl.la \
	libkcarchiver.la libkcarchivercore.la ${PROG_LIBS} ${XML2_LIBS}
if ENABLE_PYTHON
kopano_spooler_LDADD += libkcpyplug.la
endif


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
	provider/server/ECThreadManager.cpp provider/server/ECThreadManager.h \
	provider/soap/soapServer.cpp
kopano_server_LDADD = \
	libkcutil.la libkcmapi.la libkcssl.la \
	libkcserver.la libkcsoap.la ${GSOAP_LIBS} \
	${PROG_LIBS} ${clock_LIBS} \
	${CRYPTO_LIBS} ${icu_uc_LIBS} ${icu_i18n_LIBS} ${DL_LIBS} \
	${MYSQL_LIBS} ${SSL_LIBS} ${PAM_LIBS} ${KRB5_LIBS} -lpthread
kopano_server_LDFLAGS = ${AM_LDFLAGS}


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
	ECtools/archiver/ECArchiverLogger.cpp ECtools/archiver/ECArchiverLogger.h \
	ECtools/archiver/ECIterators.cpp ECtools/archiver/ECIterators.h
libkcarchiver_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	${default_SYFLAGS${NO_VSYM}}
libkcarchiver_la_LIBADD = libmapi.la libkcmapi.la libkcutil.la
EXTRA_libkcarchiver_la_DEPENDENCIES = ${default_sym}


#
# libkcarchivercore
#
libkcarchivercore_la_SOURCES = \
	ECtools/archiver/Archiver.cpp ECtools/archiver/Archiver.h \
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
	ECtools/archiver/arc_mysql.cpp ECtools/archiver/arc_mysql.hpp \
	common/database.cpp common/include/kopano/database.hpp
libkcarchivercore_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	${default_SYFLAGS${NO_VSYM}}
libkcarchivercore_la_LIBADD = \
	libmapi.la libkcmapi.la libkcarchiver.la \
	${PROG_LIBS} ${SSL_LIBS} ${MYSQL_LIBS}
EXTRA_libkcarchivercore_la_DEPENDENCIES = ${default_sym}


#
# libkcclient
#
libkcclient_la_SOURCES = \
	provider/client/ClientUtil.cpp provider/client/ClientUtil.h \
	provider/client/ECABContainer.cpp provider/client/ECABContainer.h \
	provider/client/ECABLogon.cpp provider/client/ECABLogon.h \
	provider/client/ECABProp.cpp provider/client/ECABProp.h \
	provider/client/ECABProvider.cpp provider/client/ECABProvider.h \
	provider/client/ECArchiveAwareAttach.cpp provider/client/ECArchiveAwareAttach.h \
	provider/client/ECArchiveAwareMessage.cpp provider/client/ECArchiveAwareMessage.h \
	provider/client/ECArchiveAwareMsgStore.cpp provider/client/ECArchiveAwareMsgStore.h \
	provider/client/ECAttach.cpp provider/client/ECAttach.h \
	provider/client/ECChangeAdvisor.cpp provider/client/ECChangeAdvisor.h \
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
	provider/client/ECMailUser.cpp provider/client/ECMailUser.h \
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
	provider/client/EntryPoint.h \
	provider/client/IECPropStorage.h \
	provider/client/Mem.cpp provider/client/Mem.h \
	provider/client/ProviderUtil.cpp provider/client/ProviderUtil.h \
	provider/client/SessionGroupData.cpp provider/client/SessionGroupData.h \
	provider/client/WSABPropStorage.cpp provider/client/WSABPropStorage.h \
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
	provider/client/resource.h \
	provider/common/SOAPSock.cpp provider/common/SOAPSock.h \
	provider/soap/soapClient.cpp
libkcclient_la_LIBADD = \
	libkcmapi.la libkcssl.la libkcutil.la libkcfreebusy.la libkcsync.la \
	libmapi.la ${COND_libkcrosie_la} libkcsoap.la -lstdc++ \
	${GSOAP_LIBS} ${ICU_LIBS} ${SSL_LIBS} ${UUID_LIBS} ${KRB5_LIBS}
libkcclient_la_SYFLAGS = -Wl,--version-script=provider/provider.sym
libkcclient_la_LDFLAGS = ${AM_LDFLAGS} -avoid-version -module \
	${libkcclient_la_SYFLAGS${NO_VSYM}}
EXTRA_libkcclient_la_DEPENDENCIES = provider/provider.sym


#
# libkccontacts
#
libkccontacts_la_SOURCES = \
	provider/contacts/epcontact.cpp provider/contacts/ZCABData.h \
	provider/contacts/ZCABContainer.h provider/contacts/ZCABContainer.cpp \
	provider/contacts/ZCABLogon.h provider/contacts/ZCABLogon.cpp \
	provider/contacts/ZCABProvider.h provider/contacts/ZCABProvider.cpp \
	provider/contacts/ZCMAPIProp.h provider/contacts/ZCMAPIProp.cpp
libkccontacts_la_LIBADD = \
	libkcutil.la libkcmapi.la libmapi.la -lstdc++ ${UUID_LIBS} ${ICU_LIBS}
libkccontacts_la_SYFLAGS = -Wl,--version-script=provider/provider.sym
libkccontacts_la_LDFLAGS = ${AM_LDFLAGS} -avoid-version -module \
	${libkccontacts_la_SYFLAGS${NO_VSYM}}
EXTRA_libkccontacts_la_DEPENDENCIES = provider/provider.sym


#
# libkcfreebusy
#
libkcfreebusydir = ${includedir}/libfreebusy
libkcfreebusy_HEADERS = \
	libfreebusy/ECEnumFBBlock.h libfreebusy/ECFBBlockList.h \
	libfreebusy/ECFreeBusyData.h libfreebusy/ECFreeBusySupport.h \
	libfreebusy/PublishFreeBusy.h \
	libfreebusy/freebusy.h libfreebusy/freebusyguid.h \
	libfreebusy/freebusytags.h libfreebusy/freebusyutil.h \
	libfreebusy/recurrence.h
libkcfreebusy_la_SOURCES = \
	libfreebusy/ECEnumFBBlock.cpp libfreebusy/ECFBBlockList.cpp \
	libfreebusy/ECFreeBusyData.cpp libfreebusy/ECFreeBusySupport.cpp \
	libfreebusy/ECFreeBusyUpdate.cpp libfreebusy/ECFreeBusyUpdate.h \
	libfreebusy/PublishFreeBusy.cpp \
	libfreebusy/freebusyutil.cpp libfreebusy/recurrence.cpp
libkcfreebusy_la_LIBADD = \
	libkcutil.la libkcmapi.la libmapi.la
libkcfreebusy_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	${default_SYFLAGS${NO_VSYM}}
EXTRA_libkcfreebusy_la_DEPENDENCIES = ${default_sym}


#
# libkchl
#
libkchl_la_SOURCES = mapi4linux/src/hl.cpp
libkchl_la_LIBADD = libmapi.la
libkchl_la_SYFLAGS = -Wl,--version-script=mapi4linux/src/libkchl.sym
libkchl_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined -version-info 0:0:0 \
	${libkchl_la_SYFLAGS${NO_VSYM}}
EXTRA_libkchl_la_DEPENDENCIES = mapi4linux/src/libkchl.sym


#
# libkcicalmapi
#
libkcicalmapidir = ${includedir}/icalmapi
libkcicalmapi_HEADERS = \
	libicalmapi/ICalToMAPI.h libicalmapi/MAPIToICal.h \
	libicalmapi/icalcompat.hpp \
	libicalmapi/icalitem.h libicalmapi/icalmem.hpp \
	libicalmapi/icalrecurrence.h libicalmapi/icaluid.h \
	libicalmapi/mapitovcf.hpp \
	libicalmapi/nameids.h libicalmapi/valarm.h \
	libicalmapi/vcftomapi.hpp libicalmapi/vconverter.h \
	libicalmapi/vevent.h libicalmapi/vfreebusy.h libicalmapi/vtimezone.h \
	libicalmapi/vtodo.h
libkcicalmapi_la_SOURCES = \
	libicalmapi/ICalToMAPI.cpp libicalmapi/MAPIToICal.cpp \
	libicalmapi/icalinit.cpp libicalmapi/icalrecurrence.cpp \
	libicalmapi/icaluid.cpp libicalmapi/mapitovcf.cpp \
	libicalmapi/nameids.cpp libicalmapi/valarm.cpp \
	libicalmapi/vcftomapi.cpp libicalmapi/vconverter.cpp \
	libicalmapi/vevent.cpp libicalmapi/vfreebusy.cpp \
	libicalmapi/vtimezone.cpp libicalmapi/vtodo.cpp
libkcicalmapi_la_LIBADD = \
	libkcfreebusy.la libmapi.la libkcmapi.la libkcutil.la ${ICAL_LIBS}
libkcicalmapi_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined -version-info 0:0:0 \
	${default_SYFLAGS${NO_VSYM}}
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
	inetmapi/serviceRegistration.inl \
	inetmapi/tnef.cpp inetmapi/tnef.h
# Add '-z now' -> -Wl,-z, -Wl,now to LD flags so that we do early binding of
# symbols. This fixes a problem in which libvmime attempts to resolve a symbol
# in libinetmapi.so during the unload of vmime, initiated by the unload of
# libinetmapi. On some systems, this fails to resolve the symbol during unload,
# resulting in a crash in __cxa_finalize during unload of mapi.so
libkcinetmapi_la_LIBADD = \
	libmapi.la libkcmapi.la libkcutil.la libkcicalmapi.la \
	${COND_libkcrosie_la} ${VMIME_LIBS} ${XML2_LIBS}
libkcinetmapi_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	-version-info 0:0:0 -Wl,-z -Wl,now ${default_SYFLAGS${NO_VSYM}}
EXTRA_libkcinetmapi_la_DEPENDENCIES = ${default_sym}


#
# libkcserver
#
libkcserver_la_SOURCES = \
	common/database.cpp common/include/kopano/database.hpp \
	provider/libserver/ECABObjectTable.cpp provider/libserver/ECABObjectTable.h \
	provider/libserver/ECAttachmentStorage.cpp provider/libserver/ECAttachmentStorage.h \
	provider/libserver/ECCacheManager.cpp provider/libserver/ECCacheManager.h \
	provider/libserver/ECConvenientDepthObjectTable.cpp \
	provider/libserver/ECConvenientDepthObjectTable.h \
	provider/libserver/ECDBDef.h \
	provider/libserver/ECDatabase.h \
	provider/libserver/ECDatabaseFactory.cpp provider/libserver/ECDatabaseFactory.h \
	provider/libserver/ECDatabaseMySQL.cpp \
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
	provider/libserver/ECNotification.h \
	provider/libserver/ECNotificationManager.cpp provider/libserver/ECNotificationManager.h \
	provider/libserver/ECPluginFactory.cpp provider/libserver/ECPluginFactory.h \
	provider/libserver/ECPluginSharedData.cpp \
	provider/libserver/ECS3Attachment.cpp provider/libserver/ECS3Attachment.h \
	provider/libserver/ECSearchFolders.cpp provider/libserver/ECSearchFolders.h \
	provider/libserver/ECSearchObjectTable.cpp provider/libserver/ECSearchObjectTable.h \
	provider/libserver/ECSecurity.cpp provider/libserver/ECSecurity.h \
	provider/libserver/ECServerEntrypoint.cpp provider/libserver/ECServerEntrypoint.h \
	provider/libserver/ECSession.cpp provider/libserver/ECSession.h \
	provider/libserver/ECSessionGroup.cpp provider/libserver/ECSessionGroup.h \
	provider/libserver/ECSessionManager.cpp provider/libserver/ECSessionManager.h \
	provider/libserver/ECStatsCollector.cpp provider/libserver/ECStatsCollector.h \
	provider/libserver/ECStatsTables.cpp provider/libserver/ECStatsTables.h \
	provider/libserver/ECStoreObjectTable.cpp provider/libserver/ECStoreObjectTable.h \
	provider/libserver/ECStringCompat.cpp provider/libserver/ECStringCompat.h \
	provider/libserver/ECSubRestriction.cpp provider/libserver/ECSubRestriction.h \
	provider/libserver/ECTPropsPurge.cpp provider/libserver/ECTPropsPurge.h \
	provider/libserver/ECTableManager.cpp provider/libserver/ECTableManager.h \
	provider/libserver/ECTestProtocol.cpp provider/libserver/ECTestProtocol.h \
	provider/libserver/ECUserManagement.cpp provider/libserver/ECUserManagement.h \
	provider/libserver/ECUserStoreTable.cpp provider/libserver/ECUserStoreTable.h \
	provider/libserver/StorageUtil.cpp provider/libserver/StorageUtil.h \
	provider/libserver/StreamUtil.cpp provider/libserver/StreamUtil.h \
	provider/libserver/cmd.cpp provider/libserver/cmd.hpp \
	provider/libserver/cmdutil.cpp provider/libserver/cmdutil.hpp \
	provider/libserver/logontime.cpp provider/libserver/logontime.hpp \
	provider/common/ECLicenseClient.cpp provider/common/ECLicenseClient.h \
	provider/common/ECSearchClient.cpp provider/common/ECSearchClient.h \
	provider/common/SOAPDebug.cpp provider/common/SOAPDebug.h
libkcserver_la_LIBADD = \
	libkcutil.la libkcmapi.la libkcssl.la libkcsoap.la \
	-lpthread ${icu_i18n_LIBS} ${icu_uc_LIBS} \
	${GSOAP_LIBS} ${KRB5_LIBS} ${MYSQL_LIBS} ${PAM_LIBS} ${SSL_LIBS}
libkcserver_la_SYFLAGS = -Wl,--version-script=provider/libkcserver.sym
libkcserver_la_LDFLAGS = ${AM_LDFLAGS} \
	${libkcserver_la_SYFLAGS${NO_VSYM}}
EXTRA_libkcserver_la_DEPENDENCIES = provider/libkcserver.sym

provider/libkcserver.sym: ${top_builddir}/config.status


#
# libkcmapi
#
noinst_HEADERS += \
	common/ECMemStream.h common/HtmlEntity.h common/HtmlToTextParser.h \
	common/IStreamAdapter.h common/rtfutil.h
libkcmapi_la_SOURCES = \
	common/ECABEntryID.cpp common/ECDebug.cpp \
	common/ECGuid.cpp common/ECKeyTable.cpp common/ECMemStream.cpp \
	common/ECUnknown.cpp common/HtmlEntity.cpp common/HtmlToTextParser.cpp \
	common/IStreamAdapter.cpp common/MAPIErrors.cpp common/kcodes.cpp \
	common/codepage.cpp common/rtfutil.cpp
libkcmapi_la_LIBADD = libkcutil.la -lpthread
libkcmapi_la_SYFLAGS = -Wl,--version-script=common/libkcmapi.sym
libkcmapi_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	${libkcmapi_la_SYFLAGS${NO_VSYM}}
EXTRA_libkcmapi_la_DEPENDENCIES = common/libkcmapi.sym

common/libkcmapi.sym: ${top_builddir}/config.status


#
# libkcrosie
#
include_HEADERS = librosie/librosie.h
libkcrosie_la_SOURCES = librosie/librosie.cpp
libkcrosie_la_LIBADD  = libkcutil.la -ltidy
libkcrosie_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined -version-info 0:0:0 \
	${default_SYFLAGS${NO_VSYM}}
EXTRA_libkcrosie_la_DEPENDENCIES = ${default_sym}


#
# libkcsoap
#
libkcsoap_la_SOURCES = \
	provider/common/soapctor.cpp provider/soap/soapC.cpp \
	provider/common/pcuser.cpp provider/common/SOAPAlloc.h \
	provider/common/SOAPUtils.cpp provider/common/SOAPUtils.h \
	provider/common/pcutil.cpp provider/common/pcutil.hpp \
	provider/common/versions.h
libkcsoap_la_CXXFLAGS = ${AM_CXXFLAGS} -fvisibility=default
libkcsoap_la_LIBADD = libkcutil.la libkcmapi.la ${GSOAP_LIBS}
libkcsoap_la_SYFLAGS = -Wl,--version-script=provider/libkcsoap.sym
libkcsoap_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	${libkcsoap_la_SYFLAGS${NO_VSYM}}
EXTRA_libkcsoap_la_DEPENDENCIES = provider/libkcsoap.sym

provider/libkcsoap.sym: ${top_builddir}/config.status


#
# libkcssl
#
noinst_HEADERS += common/ECChannelClient.h common/SSLUtil.h
libkcssl_la_SOURCES = \
	common/ECChannel.cpp common/ECChannelClient.cpp common/SSLUtil.cpp
libkcssl_la_LIBADD = libkcutil.la -lpthread ${SSL_LIBS}
libkcssl_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	${default_SYFLAGS${NO_VSYM}}
EXTRA_libkcssl_la_DEPENDENCIES = ${default_sym}


#
# libkcsync
#
libkcsyncdir = ${includedir}/libkcsync
libkcsync_HEADERS = \
	libsync/ECResyncSet.h libsync/ECSyncContext.h libsync/ECSyncLog.h \
	libsync/ECSyncSettings.h libsync/ECSyncUtil.h
libkcsync_la_SOURCES = \
	libsync/ECResyncSet.cpp \
	libsync/ECSyncContext.cpp libsync/ECSyncLog.cpp \
	libsync/ECSyncSettings.cpp libsync/ECSyncUtil.cpp
libkcsync_la_LIBADD = libkcmapi.la libmapi.la
libkcsync_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	${default_SYFLAGS${NO_VSYM}}
EXTRA_libkcsync_la_DEPENDENCIES = ${default_sym}


#
# libkcutil
#
noinst_HEADERS += \
	common/ConsoleTable.h common/ECACL.h common/ECCache.h common/ECConfigImpl.h \
	common/ECFifoBuffer.h common/ECSerializer.h common/StatsClient.h \
	common/SymmetricCrypt.h common/TimeUtil.h common/TmpPath.h common/fileutil.h \
	common/charset/localeutil.h \
	common/utf8/core.h common/utf8/unchecked.h
libkcutil_la_SOURCES = \
	common/ConsoleTable.cpp common/ECACL.cpp common/ECCache.cpp \
	common/ECConfig.cpp common/ECConfigImpl.cpp common/ECFifoBuffer.cpp \
	common/ECGetText.cpp common/ECIConv.cpp common/ECLogger.cpp \
	common/ECScheduler.cpp common/ECThreadPool.cpp \
	common/StatsClient.cpp common/SymmetricCrypt.cpp common/TimeUtil.cpp \
	common/TmpPath.cpp common/UnixUtil.cpp \
	common/fileutil.cpp common/my_getopt.cpp common/platform.cpp \
	common/platform.linux.cpp common/stringutil.cpp \
	common/ustringutil.cpp common/charset/convert.cpp \
	common/charset/convstring.cpp common/charset/traits.cpp \
	common/charset/localeutil.cpp
libkcutil_la_LIBADD = \
	-lpthread -lz ${CRYPTO_LIBS} ${icu_i18n_LIBS} ${icu_uc_LIBS} ${UUID_LIBS}
libkcutil_la_SYFLAGS = -Wl,--version-script=common/libkcutil.sym
libkcutil_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined \
	${libkcutil_la_SYFLAGS${NO_VSYM}}
EXTRA_libkcutil_la_DEPENDENCIES = common/libkcutil.sym


common/libkcutil.sym: ${top_builddir}/config.status


#
# libmapi
#
libmapi_la_SOURCES = \
	mapi4linux/src/common.cpp mapi4linux/src/mapidefs.cpp mapi4linux/src/mapispi.cpp mapi4linux/src/mapiutil.cpp \
	mapi4linux/src/mapix.cpp mapi4linux/src/rtf.cpp mapi4linux/src/m4l.common.h \
	mapi4linux/src/m4l.mapidefs.h mapi4linux/src/m4l.mapispi.h \
	mapi4linux/src/m4l.mapix.h mapi4linux/src/mapi4linux.h mapi4linux/src/rtf.h \
	mapi4linux/src/m4l.mapisvc.cpp mapi4linux/src/m4l.mapisvc.h \
	m4lcommon/CommonUtil.cpp \
	m4lcommon/ECFeatures.cpp m4lcommon/ECFeatureList.h \
	m4lcommon/ECMemTable.cpp m4lcommon/HrException.h \
	m4lcommon/ECRestriction.cpp \
	m4lcommon/RecurrenceState.cpp m4lcommon/userutil.cpp \
	m4lcommon/MAPIConsoleTable.cpp m4lcommon/MAPIConsoleTable.h \
	m4lcommon/MAPINotifSink.cpp m4lcommon/MAPINotifSink.h \
	m4lcommon/Util.cpp \
	m4lcommon/mapicontact.h
libmapi_la_LIBADD = \
	libkcutil.la libkcmapi.la -lpthread ${PROG_LIBS} ${DL_LIBS}
libmapi_la_SYFLAGS = -Wl,--version-script=mapi4linux/src/libmapi.sym
libmapi_la_LDFLAGS = ${AM_LDFLAGS} -no-undefined -version-info 1:0:0 \
	${libmapi_la_SYFLAGS${NO_VSYM}}
EXTRA_libmapi_la_DEPENDENCIES = mapi4linux/src/libmapi.sym

mapi4linux/src/libmapi.sym: ${top_builddir}/config.status

libm4lcommondir = ${includedir}/mapi4linux
libm4lcommon_HEADERS = m4lcommon/include/edkguid.h


#
# PHP configuration
# Old note: Debian has a broken php-config(1) script which outputs
# 	"--with-config-file-scan-dir=/etc/php5/apache2/conf.d"
# If we install our config file into that location, it will only be available
# to only apache2-mod_php5 and not php-cli, php-fpm, etc.
#
phpsysconfdir = ${dcprefix}${PHP_SYSCONF_DIR}
dist_phpsysconf_DATA = php-ext/mapi.ini


#
# PHP extension
#
phplibdir = ${dcprefix}${PHP_EXTENSION_DIR}
phplib_LTLIBRARIES =
mapi_la_CPPFLAGS = ${AM_CPPFLAGS} ${PHP_INCLUDES}
mapi_la_CXXFLAGS = ${AM_CXXFLAGS} ${ZPHPCXXFLAGS}
mapi_la_SOURCES =
if WITH_PHP7
phplib_LTLIBRARIES += mapi.la
mapi_la_CPPFLAGS += -I${srcdir}/php7-ext -I${srcdir}/php-ext
mapi_la_SOURCES += \
	php7-ext/ECImportContentsChangesProxy.cpp \
	php7-ext/ECImportContentsChangesProxy.h \
	php7-ext/ECImportHierarchyChangesProxy.cpp \
	php7-ext/ECImportHierarchyChangesProxy.h \
	php-ext/ECRulesTableProxy.cpp php-ext/ECRulesTableProxy.h \
	php-ext/globals.h php7-ext/main.cpp php-ext/main.h \
	php-ext/phpconfig.h \
	php7-ext/typeconversion.cpp php7-ext/typeconversion.h \
	php-ext/util.cpp php-ext/util.h
endif
if WITH_PHP5
phplib_LTLIBRARIES += mapi.la
mapi_la_SOURCES += \
	php-ext/ECImportContentsChangesProxy.cpp \
	php-ext/ECImportContentsChangesProxy.h \
	php-ext/ECImportHierarchyChangesProxy.cpp \
	php-ext/ECImportHierarchyChangesProxy.h \
	php-ext/ECRulesTableProxy.cpp php-ext/ECRulesTableProxy.h \
	php-ext/globals.h php-ext/main.cpp php-ext/main.h \
	php-ext/phpconfig.h \
	php-ext/typeconversion.cpp php-ext/typeconversion.h \
	php-ext/util.cpp php-ext/util.h
endif
mapi_la_LIBADD = libmapi.la libkcmapi.la libkcutil.la libkcfreebusy.la \
	libkcinetmapi.la libkcicalmapi.la ${PROG_LIBS} -lpthread
mapi_la_SYFLAGS = -Wl,--version-script=${srcdir}/php-ext/mapi.sym
mapi_la_LDFLAGS = ${AM_LDFLAGS} -avoid-version -module ${PHP_LDFLAGS} \
	${mapi_la_SYFLAGS${NO_VSYM}}
EXTRA_mapi_la_DEPENDENCIES = ${srcdir}/php-ext/mapi.sym


#
# PHP classes, config files
#
phpdatadir = ${datarootdir}/kopano/php/mapi
dist_phpdata_DATA = \
	php-ext/include/mapi/class.baseexception.php \
	php-ext/include/mapi/class.baserecurrence.php \
	php-ext/include/mapi/class.freebusypublish.php \
	php-ext/include/mapi/class.mapiexception.php \
	php-ext/include/mapi/class.meetingrequest.php \
	php-ext/include/mapi/class.recurrence.php \
	php-ext/include/mapi/class.taskrecurrence.php \
	php-ext/include/mapi/class.taskrequest.php \
	php-ext/include/mapi/mapi.util.php \
	php-ext/include/mapi/mapicode.php \
	php-ext/include/mapi/mapidefs.php \
	php-ext/include/mapi/mapiguid.php \
	php-ext/include/mapi/mapitags.php


#
# test programs
#
mapitime_SOURCES = ECtools/mapitime.cpp
mapitime_LDADD = ${clock_LIBS} ${timer_LIBS} \
	libmapi.la libkcmapi.la libkcutil.la \
	${curl_LIBS} ${icu_uc_LIBS}
rosie_test_SOURCES = librosie/test.cpp
rosie_test_LDADD = libkcrosie.la


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

BUILT_SOURCES += provider/soap/ns.xsd ${SOAP_CPP_SRC}
CLEANFILES += ${SOAP_CPP_SRC} provider/soap/*


#
# server userdb plugins
#
COMMON_PLUGIN_FILES = provider/plugins/plugin.h
COMMON_DBPLUGIN_FILES = provider/plugins/DBBase.h provider/plugins/DBBase.cpp
lib_LTLIBRARIES += libkcserver-db.la
noinst_DATA += libkcserver-db.ldd
libkcserver_db_la_SOURCES = \
	provider/plugins/DBUserPlugin.cpp provider/plugins/DBUserPlugin.h \
	${COMMON_PLUGIN_FILES} ${COMMON_DBPLUGIN_FILES}
libkcserver_db_la_LIBADD = \
	libkcutil.la libkcserver.la ${MYSQL_LIBS}
libkcserver_db_la_LDFLAGS = ${AM_LDFLAGS} -avoid-version -module \
	${plugin_SYFLAGS${NO_VSYM}}
EXTRA_libkcserver_db_la_DEPENDENCIES = provider/plugin.sym

if WITH_UNIX
lib_LTLIBRARIES += libkcserver-unix.la
noinst_DATA += libkcserver-unix.ldd
libkcserver_unix_la_SOURCES = \
	provider/plugins/UnixUserPlugin.cpp provider/plugins/UnixUserPlugin.h \
	${COMMON_PLUGIN_FILES} ${COMMON_DBPLUGIN_FILES}
libkcserver_unix_la_LIBADD = \
	libkcutil.la libkcserver.la ${MYSQL_LIBS} ${CRYPT_LIBS}
libkcserver_unix_la_LDFLAGS = ${AM_LDFLAGS} -module -avoid-version \
	${plugin_SYFLAGS${NO_VSYM}}
EXTRA_libkcserver_unix_la_DEPENDENCIES = provider/plugin.sym
endif

if WITH_LDAP
lib_LTLIBRARIES += libkcserver-ldap.la
noinst_DATA += libkcserver-ldap.ldd
libkcserver_ldap_la_SOURCES = \
	provider/plugins/LDAPUserPlugin.cpp provider/plugins/LDAPUserPlugin.h \
	provider/plugins/LDAPCache.cpp provider/plugins/LDAPCache.h \
	provider/plugins/ldappasswords.cpp provider/plugins/ldappasswords.h \
	${COMMON_PLUGIN_FILES}
libkcserver_ldap_la_LIBADD = \
	libkcutil.la \
	libkcserver.la ${CRYPTO_LIBS} ${LDAP_LIBS}
libkcserver_ldap_la_LDFLAGS = ${AM_LDFLAGS} -module -avoid-version \
	${plugin_SYFLAGS${NO_VSYM}}
EXTRA_libkcserver_ldap_la_DEPENDENCIES = provider/plugin.sym
endif


#
# manual pages
#
dist_man_MANS = \
	doc/kopano.7 \
	doc/kopano-admin.8 \
	doc/kopano-cli.8 \
	doc/kopano-archiver.8 doc/kopano-archiver.cfg.5 \
	doc/kopano-autorespond.8 \
	doc/kopano-cfgchecker.8 doc/kopano-coredump.5 \
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
	doc/kopano-unix.cfg.5 \
	doc/kopano-migration-pst.8 doc/kopano-migration-pst.cfg.5 \
	doc/kopano-archiver-aclsync.8 \
	doc/kopano-archiver-aclset.8 \
	doc/kopano-archiver-restore.8

#
# build infrastructure
#
default.sym mapi4linux/src/libkchl.sym \
provider/plugin.sym provider/provider.sym: ${top_builddir}/config.status

include global.am


#OLD TOPLEVEL FM
EXTRA_DIST += version versionheader.sh \
	AGPL-3 LICENSE.txt RELNOTES.txt \
	bootstrap.sh lddcheck global.am pwdcheck.sh \
	common/include/kopano/ecversion.h

BUILT_SOURCES += source-moved-check common/include/kopano/ecversion.h
CLEANFILES += common/include/kopano/ecversion.h

dist_sbin_SCRIPTS += tools/python-scripts/kopano-cachestat
gdbautoloaddir = ${datadir}/gdb/auto-load/${libdir}
dist_gdbautoload_DATA = tools/libmapi.so.1.0.0-gdb.py

AM_DISTCHECK_CONFIGURE_FLAGS = --with-dcprefix='$${prefix}'

.PHONY: source-moved-check
source-moved-check:
	@${top_srcdir}/pwdcheck.sh "${abs_top_builddir}"

version:
	echo "@PACKAGE_VERSION@" >version

common/include/kopano/ecversion.h: version versionheader.sh
	${MKDIR_P} common/include/kopano
	sh ${top_srcdir}/versionheader.sh >$@
