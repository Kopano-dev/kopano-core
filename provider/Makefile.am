# -*- Makefile -*-

SUBDIRS = include

AM_CPPFLAGS = ${ZCPPFLAGS} ${DEBUGFLAGS} \
	-DLDAP_THREAD_SAFE -DWITH_MULTISERVER -DPKGLIBDIR='"${pkglibdir}"' \
	-Isoap \
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
	${BOOST_CPPFLAGS} ${GSOAP_CFLAGS} ${KRB5_CFLAGS} ${LDAP_FLAGS} \
	${MYSQL_INCLUDES} ${SSL_CFLAGS} ${s3_CFLAGS} ${TCMALLOC_CFLAGS}
AM_CXXFLAGS = ${ZCXXFLAGS} -Wno-sign-compare

sbin_PROGRAMS = kopano-server
noinst_LTLIBRARIES = libkccommon.la
lib_LTLIBRARIES = libkcsoapclient.la libkcsoapserver.la libkcserver.la
pkglib_LTLIBRARIES = libkcclient.la libkccontacts.la
pkginclude_HEADERS = \
	common/include/kopano/pcuser.hpp \
	libserver/include/kopano/ECPluginSharedData.h
noinst_DATA = kopano-server.ldx libkcclient.ldd libkccontacts.ldd
CLEANFILES = ${noinst_DATA}
#noinst_DATA += libkccommon.ldd libkcserver.ldd \
#	libkcsoapclient.ldd libkcsoapserver.ldd
sysconf_configdir = ${MAPICONFIGDIR}
dist_sysconf_config_DATA = client/kopano.inf contacts/zcontacts.inf

sampleconfdir = ${docdir}/example-config
sampleconf_DATA = client/kopano.inf contacts/zcontacts.inf

# -E (-rdynamic) still needed because libkcserver.so references
# "searchfolder_restart_required", "_Z23kopano_get_server_statsPjPdS_S_"
# and "_Z18kopano_notify_doneP4soap"
#
kopano_server_LDFLAGS = ${AM_LDFLAGS} -Wl,-E
kopano_server_LDADD = \
	libkcserver.la ${GSOAP_LIBS} libkcsoapserver.la libkccommon.la \
	${top_builddir}/common/libkcutil.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/common/libkcssl.la \
	${PROG_LIBS} ${BOOST_SYSTEM_LIBS} \
	${icu_uc_LIBS} ${icu_i18n_LIBS} ${DL_LIBS} \
	${MYSQL_LIBS} ${SSL_LIBS} ${PAM_LIBS} ${KRB5_LIBS} -lrt
kopano_server_SOURCES = \
	server/ECServer.cpp \
	server/ECSoapServerConnection.cpp server/ECSoapServerConnection.h \
	server/ECThreadManager.cpp server/ECThreadManager.h

libkcclient_la_SOURCES = \
	client/ClientUtil.cpp client/ClientUtil.h \
	client/DLLGlobal.h \
	client/ECABContainer.cpp client/ECABContainer.h \
	client/ECABLogon.cpp client/ECABLogon.h \
	client/ECABProp.cpp client/ECABProp.h \
	client/ECABProvider.cpp client/ECABProvider.h \
	client/ECABProviderOffline.cpp client/ECABProviderOffline.h \
	client/ECABProviderSwitch.cpp client/ECABProviderSwitch.h \
	client/ECArchiveAwareAttach.cpp client/ECArchiveAwareAttach.h \
	client/ECArchiveAwareMessage.cpp client/ECArchiveAwareMessage.h \
	client/ECArchiveAwareMsgStore.cpp client/ECArchiveAwareMsgStore.h \
	client/ECAttach.cpp client/ECAttach.h \
	client/ECChangeAdvisor.cpp client/ECChangeAdvisor.h \
	client/ECDisplayTable.cpp client/ECDisplayTable.h \
	client/ECDistList.cpp client/ECDistList.h \
	client/ECExchangeExportChanges.cpp client/ECExchangeExportChanges.h \
	client/ECExchangeImportContentsChanges.cpp \
	client/ECExchangeImportContentsChanges.h \
	client/ECExchangeImportHierarchyChanges.cpp \
	client/ECExchangeImportHierarchyChanges.h \
	client/ECExchangeModifyTable.cpp client/ECExchangeModifyTable.h \
	client/ECExportAddressbookChanges.cpp \
	client/ECExportAddressbookChanges.h \
	client/ECGenericProp.cpp client/ECGenericProp.h \
	client/ECICS.h \
	client/ECMAPIContainer.cpp client/ECMAPIContainer.h \
	client/ECMAPIFolder.cpp client/ECMAPIFolder.h \
	client/ECMAPIFolderPublic.cpp client/ECMAPIFolderPublic.h \
	client/ECMAPIProp.cpp client/ECMAPIProp.h \
	client/ECMAPITable.cpp client/ECMAPITable.h \
	client/ECMSProvider.cpp client/ECMSProvider.h \
	client/ECMSProviderOffline.cpp client/ECMSProviderOffline.h \
	client/ECMSProviderSwitch.cpp client/ECMSProviderSwitch.h \
	client/ECMailUser.cpp client/ECMailUser.h \
	client/ECMemTablePublic.cpp client/ECMemTablePublic.h \
	client/ECMessage.cpp client/ECMessage.h \
	client/ECMessageStreamImporterIStreamAdapter.cpp \
	client/ECMessageStreamImporterIStreamAdapter.h \
	client/ECMsgStore.cpp client/ECMsgStore.h \
	client/ECMsgStorePublic.cpp client/ECMsgStorePublic.h \
	client/ECNamedProp.cpp client/ECNamedProp.h \
	client/ECNotifyClient.cpp client/ECNotifyClient.h \
	client/ECNotifyMaster.cpp client/ECNotifyMaster.h \
	client/ECParentStorage.cpp client/ECParentStorage.h \
	client/ECPropertyEntry.cpp client/ECPropertyEntry.h \
	client/ECSessionGroupManager.cpp client/ECSessionGroupManager.h \
	client/ECXPLogon.cpp client/ECXPLogon.h \
	client/ECXPProvider.cpp client/ECXPProvider.h \
	client/EntryPoint.h \
	client/IECPropStorage.h \
	client/Mem.cpp client/Mem.h \
	client/ProviderUtil.cpp client/ProviderUtil.h \
	client/SessionGroupData.cpp client/SessionGroupData.h \
	client/WSABPropStorage.cpp client/WSABPropStorage.h \
	client/WSABTableView.cpp client/WSABTableView.h \
	client/WSMAPIFolderOps.cpp client/WSMAPIFolderOps.h \
	client/WSMAPIPropStorage.cpp client/WSMAPIPropStorage.h \
	client/WSMessageStreamExporter.cpp client/WSMessageStreamExporter.h \
	client/WSMessageStreamImporter.cpp client/WSMessageStreamImporter.h \
	client/WSSerializedMessage.cpp client/WSSerializedMessage.h \
	client/WSStoreTableView.cpp client/WSStoreTableView.h \
	client/WSTableView.cpp client/WSTableView.h \
	client/WSTransport.cpp client/WSTransport.h \
	client/WSUtil.cpp client/WSUtil.h \
	client/epclient.cpp \
	client/resource.h
libkcclient_la_LDFLAGS = ${AM_LDFLAGS} -avoid-version -module
libkcclient_la_LIBADD = \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/common/libkcssl.la \
	${top_builddir}/common/libkcutil.la \
	${top_builddir}/libfreebusy/libkcfreebusy.la \
	${top_builddir}/libsync/libkcsync.la \
	${top_builddir}/mapi4linux/src/libmapi.la \
	libkccommon.la libkcsoapclient.la -lstdc++ \
	${GSOAP_LIBS} ${ICU_LIBS} ${SSL_LIBS} ${UUID_LIBS}

libkccommon_la_SOURCES = \
	common/ECLicenseClient.h common/ECLicenseClient.cpp \
	common/ECSearchClient.h common/ECSearchClient.cpp \
	common/SOAPAlloc.h common/SOAPDebug.cpp common/SOAPDebug.h \
	common/SOAPHelpers.cpp common/SOAPHelpers.h \
	common/SOAPSock.cpp common/SOAPSock.h \
	common/SOAPUtils.cpp common/SOAPUtils.h \
	common/pcuser.cpp common/pcutil.cpp common/pcutil.hpp \
	common/soapctor.cpp common/versions.h
libkccommon_la_LIBADD = \
	../common/libkcutil.la ../common/libkcmapi.la libkcsoapclient.la \
	../common/libkcssl.la ${GSOAP_LIBS} ${SSL_LIBS}
libkccommon_la_LDFLAGS = ${AM_LDFLAGS} \
	-Wl,--version-script=${top_builddir}/default.sym
EXTRA_libkccommon_la_DEPENDENCIES = ${top_builddir}/default.sym

libkccontacts_la_SOURCES = \
	contacts/epcontact.cpp contacts/ZCABData.h \
	contacts/ZCABContainer.h contacts/ZCABContainer.cpp \
	contacts/ZCABLogon.h contacts/ZCABLogon.cpp \
	contacts/ZCABProvider.h contacts/ZCABProvider.cpp \
	contacts/ZCMAPIProp.h contacts/ZCMAPIProp.cpp
libkccontacts_la_LDFLAGS = ${AM_LDFLAGS} -avoid-version -module
libkccontacts_la_LIBADD = \
	${top_builddir}/common/libkcutil.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/mapi4linux/src/libmapi.la \
	-lstdc++ $(UUID_LIBS) $(ICU_LIBS)

libkcserver_la_SOURCES = \
	libserver/ECABObjectTable.cpp libserver/ECABObjectTable.h \
	libserver/ECAttachmentStorage.cpp libserver/ECAttachmentStorage.h \
	libserver/ECCacheManager.cpp libserver/ECCacheManager.h \
	libserver/ECConvenientDepthABObjectTable.cpp \
	libserver/ECConvenientDepthABObjectTable.h \
	libserver/ECConvenientDepthObjectTable.cpp \
	libserver/ECConvenientDepthObjectTable.h \
	libserver/ECConversion.cpp libserver/ECConversion.h \
	libserver/ECDBDef.h \
	libserver/ECDatabase.h \
	libserver/ECDatabaseFactory.cpp libserver/ECDatabaseFactory.h \
	libserver/ECDatabaseMySQL.cpp libserver/ECDatabaseMySQL.h \
	libserver/ECDatabaseUpdate.cpp libserver/ECDatabaseUpdate.h \
	libserver/ECDatabaseUtils.cpp libserver/ECDatabaseUtils.h \
	libserver/ECGenProps.cpp libserver/ECGenProps.h \
	libserver/ECGenericObjectTable.cpp \
	libserver/ECGenericObjectTable.h \
	libserver/ECICS.cpp libserver/ECICS.h \
	libserver/ECICSHelpers.cpp libserver/ECICSHelpers.h \
	libserver/ECIndexer.cpp libserver/ECIndexer.h \
	libserver/ECKrbAuth.cpp libserver/ECKrbAuth.h \
	libserver/ECLockManager.cpp libserver/ECLockManager.h \
	libserver/ECMAPI.h \
	libserver/ECMailBoxTable.cpp libserver/ECMailBoxTable.h \
	libserver/ECMultiStoreTable.cpp libserver/ECMultiStoreTable.h \
	libserver/ECNotification.cpp libserver/ECNotification.h \
	libserver/ECNotificationManager.cpp libserver/ECNotificationManager.h \
	libserver/ECPamAuth.cpp libserver/ECPamAuth.h \
	libserver/ECPluginFactory.cpp libserver/ECPluginFactory.h \
	libserver/ECPluginSharedData.cpp \
	libserver/ECS3Attachment.cpp libserver/ECS3Attachment.h \
	libserver/ECSearchFolders.cpp libserver/ECSearchFolders.h \
	libserver/ECSearchObjectTable.cpp libserver/ECSearchObjectTable.h \
	libserver/ECSecurity.cpp libserver/ECSecurity.h \
	libserver/ECSecurityOffline.cpp libserver/ECSecurityOffline.h \
	libserver/ECServerEntrypoint.cpp libserver/ECServerEntrypoint.h \
	libserver/ECSession.cpp libserver/ECSession.h \
	libserver/ECSessionGroup.cpp libserver/ECSessionGroup.h \
	libserver/ECSessionManager.cpp libserver/ECSessionManager.h \
	libserver/ECSessionManagerOffline.cpp \
	libserver/ECSessionManagerOffline.h \
	libserver/ECStatsCollector.cpp libserver/ECStatsCollector.h \
	libserver/ECStatsTables.cpp libserver/ECStatsTables.h \
	libserver/ECStoreObjectTable.cpp libserver/ECStoreObjectTable.h \
	libserver/ECStringCompat.cpp libserver/ECStringCompat.h \
	libserver/ECSubRestriction.cpp libserver/ECSubRestriction.h \
	libserver/ECTPropsPurge.cpp libserver/ECTPropsPurge.h \
	libserver/ECTableManager.cpp libserver/ECTableManager.h \
	libserver/ECTestProtocol.cpp libserver/ECTestProtocol.h \
	libserver/ECUserManagement.cpp libserver/ECUserManagement.h \
	libserver/ECUserManagementOffline.cpp \
	libserver/ECUserManagementOffline.h \
	libserver/ECUserStoreTable.cpp libserver/ECUserStoreTable.h \
	libserver/StorageUtil.cpp libserver/StorageUtil.h \
	libserver/StreamUtil.cpp libserver/StreamUtil.h \
	libserver/cmd.cpp libserver/cmd.hpp \
	libserver/cmdutil.cpp libserver/cmdutil.hpp \
	libserver/logontime.cpp libserver/logontime.hpp
libkcserver_la_LIBADD = \
	../common/libkcutil.la libkccommon.la \
	-lpthread ${icu_i18n_LIBS} ${icu_uc_LIBS} \
	${KRB5_LIBS} ${MYSQL_LIBS} ${PAM_LIBS} ${SSL_LIBS}
libkcserver_la_LDFLAGS = ${AM_LDFLAGS} \
	-Wl,--version-script=${top_builddir}/default.sym
EXTRA_libkcserver_la_DEPENDENCIES = ${top_builddir}/default.sym

nodist_libkcsoapclient_la_SOURCES = soap/soapC.cpp soap/soapClient.cpp
libkcsoapclient_la_LIBADD = ${GSOAP_LIBS}
libkcsoapclient_la_LDFLAGS = ${AM_LDFLAGS} \
	-Wl,--version-script=${top_builddir}/default.sym
EXTRA_libkcsoapclient_la_DEPENDENCIES = ${top_builddir}/default.sym

nodist_libkcsoapserver_la_SOURCES = soap/soapC.cpp soap/soapServer.cpp
libkcsoapserver_la_LIBADD = ${GSOAP_LIBS}
libkcsoapserver_la_LDFLAGS = ${AM_LDFLAGS} \
	-Wl,--version-script=${top_builddir}/default.sym
EXTRA_libkcsoapserver_la_DEPENDENCIES = ${top_builddir}/default.sym

SOAPHEADER = include/proto.h
SOAP_NS = soap/KCmd.nsmap soap/KCmd.wsdl
SOAP_H_FILES = soap/soapH.h soap/soapStub.h
SOAP_CPP_CLIENT = soap/soapClient.cpp soap/soapC.cpp
SOAP_LIB_FILES = soap/soapClientLib.cpp soap/soapServerLib.cpp \
	soap/soapKCmdObject.h
SOAP_CPP_SRC = ${SOAP_CPP_CLIENT} soap/soapServer.cpp ${SOAP_H_FILES} \
	${SOAP_NS} ${SOAP_LIB_FILES}

${SOAP_CPP_SRC}: soap/ns.xsd
soap/ns.xsd: ${SOAPHEADER}
	${MKDIR_P} soap
	soapcpp2 -z1 -x -Iinclude/ -d soap/ $<

BUILT_SOURCES = soap/ns.xsd ${SOAP_CPP_SRC}
CLEANFILES += ${SOAP_CPP_SRC} soap/*.req.xml soap/*.res.xml soap/*.wsdl \
	soap/*.nsmap soap/*.xsd soap/*Proxy\.h soap/*~

COMMON_PLUGIN_FILES = plugins/plugin.h
COMMON_DBPLUGIN_FILES = plugins/DBBase.h plugins/DBBase.cpp

# default plugin
pkglib_LTLIBRARIES += dbplugin.la
#noinst_DATA += dbplugin.ldd
dbplugin_la_SOURCES = \
	plugins/DBUserPlugin.cpp plugins/DBUserPlugin.h \
	${COMMON_PLUGIN_FILES} ${COMMON_DBPLUGIN_FILES}
dbplugin_la_LDFLAGS = -module -avoid-version
dbplugin_la_LIBADD = \
	${top_builddir}/common/libkcutil.la \
	libkcserver.la ${MYSQL_LIBS}

if WITH_UNIX
pkglib_LTLIBRARIES += unixplugin.la
#noinst_DATA += unixplugin.ldd
unixplugin_la_SOURCES = \
	plugins/UnixUserPlugin.cpp plugins/UnixUserPlugin.h \
	${COMMON_PLUGIN_FILES} ${COMMON_DBPLUGIN_FILES}
unixplugin_la_LDFLAGS = ${AM_LDFLAGS} -module -avoid-version
unixplugin_la_LIBADD = \
	${top_builddir}/common/libkcutil.la \
	libkcserver.la ${MYSQL_LIBS} ${CRYPT_LIBS}
endif

if WITH_LDAP
pkglib_LTLIBRARIES += ldapplugin.la
#noinst_DATA += ldapplugin.ldd
noinst_DATA += .libs/ldapmsplugin.so
ldapplugin_la_SOURCES = \
	plugins/LDAPUserPlugin.cpp plugins/LDAPUserPlugin.h \
	plugins/LDAPCache.cpp plugins/LDAPCache.h \
	plugins/ldappasswords.cpp plugins/ldappasswords.h \
	${COMMON_PLUGIN_FILES}
ldapplugin_la_LDFLAGS = ${AM_LDFLAGS} -module -avoid-version
ldapplugin_la_LIBADD = \
	${top_builddir}/common/libkcutil.la \
	libkcserver.la ${CRYPTO_LIBS} ${LDAP_LIBS}

.libs/ldapmsplugin.so: ldapplugin.la
	${AM_V_GEN}ln -fs ldapplugin.so .libs/ldapmsplugin.so

install-exec-local:
	${MKDIR_P} ${DESTDIR}${pkglibdir}
	ln -fs ldapplugin.so ${DESTDIR}${pkglibdir}/ldapmsplugin.so
endif

check-syntax:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
		-fsyntax-only -fmessage-length=0 ${CHK_SOURCES} -Wall -Wformat=2

include ../global.am
