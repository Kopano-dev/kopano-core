# -*- Makefile -*-

AM_CPPFLAGS = ${ZCPPFLAGS} $(DEBUGFLAGS) $(BOOST_CPPFLAGS) \
	-I${top_srcdir}/mapi4linux/include \
	-I${top_srcdir}/provider/libserver -I${top_srcdir}/provider/plugins \
	-I${top_srcdir}/provider/include -I${top_srcdir}/provider/common \
	-I${top_srcdir}/provider/client -I${top_builddir}/provider/soap \
	-I${top_srcdir}/provider/soap \
	-I${top_srcdir}/common -I${top_srcdir}/m4lcommon \
	-DPKGLIBDIR='"${pkglibdir}"' \
	$(MYSQL_INCLUDES) $(GSOAP_CFLAGS) $(SSL_CFLAGS) $(SSL_HAS_EVP_PKEY_CMP) $(EPOLL_CFLAGS) $(KRB5_CFLAGS)

sbin_PROGRAMS = kopano-server
noinst_DATA = kopano-server.ldx

# -E (-rdynamic) still needed because libkcserver.so references
# "searchfolder_restart_required", "_Z23kopano_get_server_statsPjPdS_S_"
# and "_Z18kopano_notify_doneP4soap"
#
kopano_server_LDFLAGS = ${AM_LDFLAGS} -Wl,-E $(BOOST_FILESYSTEM_LDFLAGS)
kopano_server_LDADD = \
	${top_builddir}/provider/libserver/libkcserver.la \
    ${GSOAP_LIBS} \
	${top_builddir}/provider/soap/libkcsoapserver.la \
	${top_builddir}/provider/common/libkccommon.la						\
	${top_builddir}/common/libkcutil.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/common/libkcssl.la \
	$(PROG_LIBS) ${icu_uc_LIBS} \
	$(BOOST_FILESYSTEM_LIBS) $(BOOST_SYSTEM_LIBS) $(ICU_LIBS) $(DL_LIBS)	\
	$(MYSQL_LIBS) $(SSL_LIBS) $(PAM_LIBS) $(KRB5_LIBS) -lrt

kopano_server_SOURCES = ECServer.cpp  \
						ECSoapServerConnection.h ECSoapServerConnection.cpp \
						ECClientUpdate.h ECClientUpdate.cpp \
						ECThreadManager.h ECThreadManager.cpp

check-syntax:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
		-fsyntax-only -fmessage-length=0 ${CHK_SOURCES} -Wall -Wformat=2

include ../../global.am
