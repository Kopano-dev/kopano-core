# -*- Makefile -*-

pkglib_LTLIBRARIES = libkcclient.la
noinst_DATA = libkcclient.ldd

sysconf_configdir = ${MAPICONFIGDIR}
dist_sysconf_config_DATA = kopano.inf

sampleconfdir = ${docdir}/example-config
sampleconf_DATA = kopano.inf

AM_CPPFLAGS = ${ZCPPFLAGS} $(DEBUGFLAGS) $(BOOST_CPPFLAGS)	\
	$(GSOAP_CFLAGS) $(SSL_CFLAGS)				\
	-I${top_srcdir}/provider/include			\
	-I${top_srcdir}/provider/common				\
	-I${top_builddir}/provider/soap				\
	-I${top_srcdir}/provider/soap				\
	-I${top_srcdir}/mapi4linux/include			\
	-I${top_srcdir}/libfreebusy					\
	-I${top_srcdir}/common -I${top_srcdir}/m4lcommon \
	-I${top_srcdir}/libsync

libkcclient_la_LDFLAGS = ${AM_LDFLAGS} -avoid-version -module
libkcclient_la_LIBADD = $(SSL_LIBS)	\
    ${GSOAP_LIBS} \
	${top_builddir}/provider/soap/libkcsoapclient.la \
	${top_builddir}/provider/common/libkccommon.la	\
	${top_builddir}/common/libkcutil.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/common/libkcssl.la \
	${top_builddir}/libfreebusy/libkcfreebusy.la \
	${top_builddir}/mapi4linux/src/libmapi.la			\
	${top_builddir}/libsync/libkcsync.la \
	-lstdc++ $(UUID_LIBS) $(ICU_LIBS)

libkcclient_la_SOURCES = ECABLogon.cpp ECABProvider.cpp											\
	ECAttach.cpp ECMAPIContainer.cpp ECMAPIFolder.cpp ECMAPIProp.cpp								\
	ECMAPITable.cpp ECMessage.cpp ECMsgStore.cpp													\
	ECMSProvider.cpp ECNamedProp.cpp ECNotifyClient.cpp ECNotifyMaster.cpp							\
	SessionGroupData.cpp ECSessionGroupManager.cpp ECXPLogon.cpp ECXPProvider.cpp epclient.cpp \
	Mem.cpp WSTransport.cpp																			\
	ECPropertyEntry.cpp ECGenericProp.cpp ECParentStorage.cpp										\
	ECABProp.cpp ECABContainer.cpp ECDistList.cpp ECMailUser.cpp									\
	WSMAPIPropStorage.cpp WSMAPIFolderOps.cpp WSTableView.cpp										\
	WSABPropStorage.cpp WSUtil.cpp											\
	WSABTableView.cpp WSStoreTableView.cpp															\
	DLLGlobal.h ECABLogon.h ECABProvider.h ECAttach.h ECMAPIContainer.h								\
	ECMAPIFolder.h ECMAPIProp.h ECMAPITable.h ECMessage.h											\
	ECMsgStore.h ECMSProvider.h ECNamedProp.h ECNotifyClient.h SessionGroupData.h ECSessionGroupManager.h	\
	ECXPLogon.h ECXPProvider.h EntryPoint.h Mem.h resource.h WSTransport.h					\
	IECPropStorage.h ECPropertyEntry.h ECGenericProp.h ECParentStorage.h							\
	ECABProp.h ECABContainer.h ECDistList.h ECMailUser.h											\
	WSMAPIPropStorage.h WSMAPIFolderOps.h WSTableView.h												\
	WSABPropStorage.h ECNotifyMaster.h WSUtil.h									\
	WSABTableView.h WSStoreTableView.h																\
	ClientUtil.h ClientUtil.cpp																		\
	ECExchangeModifyTable.cpp ECExchangeModifyTable.h												\
	ECExchangeExportChanges.cpp ECExchangeExportChanges.h											\
	ECExchangeImportContentsChanges.cpp ECExchangeImportContentsChanges.h							\
	ECExchangeImportHierarchyChanges.cpp ECExchangeImportHierarchyChanges.h							\
	ECICS.h 																		\
	ECMSProviderSwitch.cpp ECMSProviderSwitch.h	ECMSProviderOffline.h ECMSProviderOffline.cpp		\
	ECABProviderSwitch.cpp ECABProviderSwitch.h	ECABProviderOffline.h ECABProviderOffline.cpp		\
	ProviderUtil.h ProviderUtil.cpp																	\
	ECExportAddressbookChanges.h ECExportAddressbookChanges.cpp										\
	ECMAPIFolderPublic.h ECMAPIFolderPublic.cpp														\
	ECMemTablePublic.h ECMemTablePublic.cpp															\
	ECMsgStorePublic.h ECMsgStorePublic.cpp															\
	ECChangeAdvisor.h ECChangeAdvisor.cpp											\
	ECDisplayTable.h ECDisplayTable.cpp	\
	ECArchiveAwareMsgStore.h ECArchiveAwareMsgStore.cpp	\
	ECArchiveAwareMessage.h ECArchiveAwareMessage.cpp	\
	ECArchiveAwareAttach.h ECArchiveAwareAttach.cpp	\
	WSMessageStreamExporter.h WSMessageStreamExporter.cpp \
	WSSerializedMessage.h WSSerializedMessage.cpp \
	WSMessageStreamImporter.h WSMessageStreamImporter.cpp \
	ECMessageStreamImporterIStreamAdapter.h ECMessageStreamImporterIStreamAdapter.cpp

check-syntax:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
		-fsyntax-only -fmessage-length=0 ${CHK_SOURCES} -Wall -Wformat=2

include ../../global.am
