# -*- Makefile -*-

AM_CPPFLAGS = ${ZCPPFLAGS} ${DEBUGFLAGS} ${BOOST_CPPFLAGS} \
	-I${top_srcdir}/mapi4linux/include \
	-I${top_srcdir}/provider/libserver -I${top_srcdir}/provider/plugins \
	-I${top_srcdir}/provider/include -I${top_srcdir}/provider/common \
	-I${top_srcdir}/provider/client -I${top_builddir}/provider/soap \
	-I${top_srcdir}/provider/soap \
	-I${top_srcdir}/common -I${top_srcdir}/m4lcommon \
	-DPKGLIBDIR='"${pkglibdir}"' \
	${MYSQL_INCLUDES} ${GSOAP_CFLAGS} ${SSL_CFLAGS} \
	${SSL_HAS_EVP_PKEY_CMP} ${KRB5_CFLAGS} ${TCMALLOC_CFLAGS}

# We are not actually using any linked symbols from tcmalloc's header files,
# so linking to tcmalloc is not done, not here at least. It is done in
# provider/*/Makefile.am. Probably such that other components including
# libkcserver.la (provider/plugins/) _don't_ get tcmalloc.

pkginclude_HEADERS = include/kopano/ECPluginSharedData.h

lib_LTLIBRARIES = libkcserver.la
#noinst_DATA = libkcserver.ldd

libkcserver_la_SOURCES = \
	cmd.cpp cmd.hpp \
	ECABObjectTable.cpp ECABObjectTable.h \
	ECCacheManager.cpp ECCacheManager.h \
	ECDBDef.h ECDatabase.h \
	ECDatabaseFactory.cpp ECDatabaseFactory.h \
	ECDatabaseMySQL.cpp ECDatabaseMySQL.h \
	ECDatabaseUtils.cpp ECDatabaseUtils.h \
	ECDatabaseUpdate.cpp ECDatabaseUpdate.h \
	ECGenProps.cpp ECGenProps.h \
	ECGenericObjectTable.cpp ECGenericObjectTable.h \
	ECICS.cpp ECICS.h \
	ECICSHelpers.cpp ECICSHelpers.h \
	ECMAPI.h \
	ECNotification.cpp ECNotification.h \
	ECPluginFactory.cpp ECPluginFactory.h \
	ECPluginSharedData.cpp ECPluginSharedData.h \
	ECPamAuth.cpp ECPamAuth.h \
	ECKrbAuth.cpp ECKrbAuth.h \
	ECSecurity.cpp ECSecurity.h \
	ECServerEntrypoint.cpp ECServerEntrypoint.h \
	ECSession.cpp ECSession.h \
	ECSessionGroup.cpp ECSessionGroup.h \
	ECSessionManager.cpp ECSessionManager.h \
	ECStoreObjectTable.cpp ECStoreObjectTable.h \
	ECStringCompat.cpp ECStringCompat.h \
	ECSubRestriction.cpp ECSubRestriction.h \
	ECTableManager.cpp ECTableManager.h \
	ECUserManagement.cpp ECUserManagement.h \
	ECSessionManagerOffline.cpp ECSessionManagerOffline.h \
	ECUserManagementOffline.cpp ECUserManagementOffline.h \
	ECSecurityOffline.cpp ECSecurityOffline.h \
	ECSearchFolders.cpp ECSearchFolders.h \
	ECSearchObjectTable.cpp ECSearchObjectTable.h \
	ECConvenientDepthObjectTable.cpp ECConvenientDepthObjectTable.h \
	ECConvenientDepthABObjectTable.cpp ECConvenientDepthABObjectTable.h \
	ECMultiStoreTable.cpp ECMultiStoreTable.h \
	ECUserStoreTable.cpp ECUserStoreTable.h \
	ECConversion.cpp ECConversion.h \
	ECS3Attachment.cpp ECS3Attachment.h \
	ECAttachmentStorage.cpp ECAttachmentStorage.h \
	ECStatsCollector.cpp ECStatsCollector.h \
	ECStatsTables.cpp ECStatsTables.h \
	ECNotificationManager.cpp ECNotificationManager.h \
	ECIndexer.cpp ECIndexer.h \
	StreamUtil.cpp StreamUtil.h \
	StorageUtil.cpp StorageUtil.h \
	ECTPropsPurge.cpp ECTPropsPurge.h \
	ECMailBoxTable.cpp ECMailBoxTable.h \
	ECTestProtocol.cpp ECTestProtocol.h \
	cmdutil.cpp cmdutil.hpp \
	ECLockManager.cpp ECLockManager.h
libkcserver_la_LIBADD = \
	../../common/libkcutil.la ../common/libkccommon.la \
	-lpthread ${icu_i18n_LIBS} ${icu_uc_LIBS} \
	${KRB5_LIBS} ${MYSQL_LIBS} ${PAM_LIBS} ${SSL_LIBS} \
	${TCMALLOC_LIBS} ${s3_LIBS}
libkcserver_la_LDFLAGS = ${AM_LDFLAGS} \
	-Wl,--version-script=${top_builddir}/default.sym
EXTRA_libkcserver_la_DEPENDENCIES = ${top_builddir}/default.sym


check-syntax:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
		-fsyntax-only -fmessage-length=0 ${CHK_SOURCES} -Wall -Wformat=2

include ../../global.am
