AM_CPPFLAGS = ${ZCPPFLAGS} $(DEBUGFLAGS) $(BOOST_CPPFLAGS) \
	-I${top_srcdir}/common -I${top_srcdir}/m4lcommon \
	-I${top_srcdir}/provider/libserver \
	-I${top_srcdir}/provider/common	-I${top_srcdir}/mapi4linux/include

COMMON_PLUGIN_FILES = plugin.h
COMMON_DBPLUGIN_FILES = DBBase.h DBBase.cpp


pkglib_LTLIBRARIES = 
noinst_DATA =

if WITH_UNIX
pkglib_LTLIBRARIES += unixplugin.la
#noinst_DATA += unixplugin.ldd
unixplugin_la_CPPFLAGS = ${AM_CPPFLAGS} $(MYSQL_INCLUDES)
unixplugin_la_SOURCES = UnixUserPlugin.cpp UnixUserPlugin.h $(COMMON_PLUGIN_FILES) $(COMMON_DBPLUGIN_FILES)
unixplugin_la_LDFLAGS = ${AM_LDFLAGS} -module -avoid-version
unixplugin_la_LIBADD = \
	${top_builddir}/common/libkcutil.la \
	../libserver/libkcserver.la ${MYSQL_LIBS} ${CRYPT_LIBS}
endif

if WITH_LDAP
pkglib_LTLIBRARIES += ldapplugin.la
#noinst_DATA += ldapplugin.ldd
noinst_DATA += .libs/ldapmsplugin.so
ldapplugin_la_CPPFLAGS = ${AM_CPPFLAGS} $(LDAP_FLAGS) -DLDAP_THREAD_SAFE -DWITH_MULTISERVER
ldapplugin_la_SOURCES = LDAPUserPlugin.cpp LDAPUserPlugin.h LDAPCache.cpp LDAPCache.h ldappasswords.cpp ldappasswords.h $(COMMON_PLUGIN_FILES)
ldapplugin_la_LDFLAGS = ${AM_LDFLAGS} -module -avoid-version
ldapplugin_la_LIBADD = \
	${top_builddir}/common/libkcutil.la \
	../libserver/libkcserver.la ${CRYPTO_LIBS} ${LDAP_LIBS}
dist_pkgdata_DATA = ${ldapplugin_la_SOURCES}

.libs/ldapmsplugin.so: ldapplugin.la
	${AM_V_GEN}ln -fs ldapplugin.so .libs/ldapmsplugin.so

install-exec-local:
	ln -fs ldapplugin.so ${DESTDIR}${pkglibdir}/ldapmsplugin.so

endif

# default plugin
pkglib_LTLIBRARIES += dbplugin.la
#noinst_DATA += dbplugin.ldd
dbplugin_la_CPPFLAGS = ${AM_CPPFLAGS} $(MYSQL_INCLUDES)
dbplugin_la_SOURCES = DBUserPlugin.cpp DBUserPlugin.h $(COMMON_PLUGIN_FILES) $(COMMON_DBPLUGIN_FILES)
dbplugin_la_LDFLAGS = -module -avoid-version
dbplugin_la_LIBADD = \
	${top_builddir}/common/libkcutil.la \
	../libserver/libkcserver.la ${MYSQL_LIBS}

check-syntax:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) $(LDAP_FLAGS) \
		-fsyntax-only -fmessage-length=0 ${CHK_SOURCES} -Wall -Wformat=2

include ../../global.am
