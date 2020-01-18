AC_PREREQ(2.60)
AC_INIT([kopano], [10.0.1], [development@kopano.io])
dnl Change when there was an ABI change (use abidiff or abi-compare)
PACKAGE_ABI=10.0
AC_CONFIG_SRCDIR([provider/server/ECServer.cpp])
AC_CONFIG_HEADERS([common/config.h common/include/kopano/buildconfig.h])
AC_CONFIG_AUX_DIR([autoconf])
AC_CONFIG_MACRO_DIR([autoconf])
AM_INIT_AUTOMAKE([1.10 foreign subdir-objects])
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

PKG_PROG_PKG_CONFIG

dnl Directories
uscriptlibdir='${prefix}/lib/kopano/userscripts'
uscriptconfdir='${sysconfdir}/kopano/userscripts'
AC_SUBST([uscriptlibdir])
AC_SUBST([uscriptconfdir])

AC_ARG_WITH(quotatemplate-prefix,
	[AS_HELP_STRING([--with-quotatemplate-prefix=PATH], [Path to install the quota warning templates in (default SYSCONFDIR/kopano/quotamail)])],
	[QUOTATEMPLATEDIR="$withval"], [QUOTATEMPLATEDIR='${sysconfdir}/kopano/quotamail'])
AC_SUBST([QUOTATEMPLATEDIR])
AC_ARG_WITH(searchscripts-prefix,
	[AS_HELP_STRING([--with-searchscripts-prefix=PATH], [path to install the search scripts in (default SYSCONFDIR/kopano/searchscripts)])],
	[SEARCHSCRIPTSDIR="$withval"], [SEARCHSCRIPTSDIR='${sysconfdir}/kopano/searchscripts'])
AC_SUBST([SEARCHSCRIPTSDIR])
AC_ARG_WITH([systemunitdir],
	[AS_HELP_STRING([--with-systemunitdir=PATH], [Path to systemd service unit directory])],
	[systemunitdir="$withval"],
	[
		systemunitdir=$("$PKG_CONFIG" systemd --variable systemdsystemunitdir 2>/dev/null)
		AS_IF([test -z "$systemunitdir"], [systemunitdir='${prefix}/lib/systemd/system'])
	])
AC_SUBST([systemunitdir])
AC_ARG_WITH([sysusersdir],
	[AS_HELP_STRING([--with-sysusersdir=PATH], [Path to sysusers config directory])],
	[sysusersdir="$withval"],
	[
		sysusersdir=$("$PKG_CONFIG" systemd --variable sysusersdir 2>/dev/null)
		AS_IF([test -z "$sysusersdir"], [sysusersdir='${prefix}/lib/sysusers.d'])
	])
AC_SUBST([sysusersdir])
AC_ARG_WITH([tmpfilesdir],
	[AS_HELP_STRING([--with-tmpfilesdir=PATH], [Path to tmpfiles config directory])],
	[tmpfilesdir="$withval"],
	[
		tmpfilesdir=$("$PKG_CONFIG" systemd --variable tmpfilesdir 2>/dev/null)
		AS_IF([test -z "$tmpfilesdir"], [tmpfilesdir='${prefix}/lib/tmpfiles.d'])
	])
AC_SUBST([tmpfilesdir])
dnl Not all files will be installed to ${prefix}, so we need to cheat a bit
dnl to make "distcheck" succeed.
AC_ARG_WITH([dcprefix],
	[AS_HELP_STRING([Extra path inserted for distcheck])],
	[dcprefix="$withval"])
AC_SUBST([dcprefix])

dnl Python interpreter selection
AS_IF([test -n "$PYTHON"], [
	dnl Expand e.g. PYTHON="python3 -B" to PYTHON="/usr/bin/python3 -B"
	dnl (for embedding into #!), but keep unresolvable values as-is.
	set -- $PYTHON
	PYTHON=$(which "$1" 2>/dev/null)
	AS_IF([test -z "$PYTHON"], [PYTHON="$1"])
	shift
	PYTHON=$(echo "$PYTHON $@")
	PYTHON_MAJOR_VERSION=$($PYTHON -c "from __future__ import print_function;import sys;print(sys.version_info[[0]])" 2>/dev/null)
])
AS_IF([test "$PYTHON_MAJOR_VERSION" = "3"], [PYTHON_PC="python3"], [PYTHON_PC="python"])
AM_CONDITIONAL([WITH_PYTHON3], [test "x$PYTHON_MAJOR_VERSION" = "x3"])

dnl Top-level component selection
AC_ARG_ENABLE([base], [AS_HELP_STRING([--disable-base],
	[Disables building base components (default: enable)])],
	[want_base="$enableval"], [want_base=yes])
AM_CONDITIONAL([ENABLE_BASE], [test "$want_base" != no])
AC_ARG_ENABLE([python], [AS_HELP_STRING([--disable-python],
	[Disables building Python components (default: enable). Use the $PYTHON environment variable to select the script interpreter.])],
	[want_python="$enableval"], [want_python=auto])
AM_CONDITIONAL([ENABLE_PYTHON], [test "$want_python" != no])

ECCLIENT_LIBRARY_VERSION=1:0:0
AC_SUBST(ECCLIENT_LIBRARY_VERSION)

dnl Checks for programs. Must be top-level (outside our want_base block).
AC_DISABLE_STATIC	dnl must be before ac_prog_libtool
AC_PROG_LIBTOOL
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_CXX
AC_PROG_INSTALL
dnl Needed, at the very least, for `make dist` (hence outside enable_base)
AC_PATH_PROG([SWIG_EXEC], [swig])

# configure may rerun as part of git-checking out a different version,
# so remove stale version
rm -f version
AC_SUBST([PACKAGE_ABI])
abs_top_srcdir=`cd $srcdir; pwd`
abs_top_builddir=`pwd`

ZCPPFLAGS=""
AC_CANONICAL_HOST
AS_CASE([$host_os],
	[openbsd*], [
		BUILDOS="openbsd"
		ZCPPFLAGS="$ZCPPFLAGS -DOPENBSD"
		LN_TFSV="ln -fs"
	],
	[linux*], [
		BUILDOS="linux"
		ZCPPFLAGS="$ZCPPFLAGS -DLINUX"
		LN_TFSV="ln -Tfsv"
	],
	[*], [
		LN_TFSV="ln -fs"
	])

AC_ARG_ENABLE([dovecot], [AS_HELP_STRING([--enable-dovecot], [Build dovecot modules (default: disabled)])])
AM_CONDITIONAL([WITH_DOVECOT], [test "$enable_dovecot" = yes])

AS_IF([test "$want_base" != no], [

#
# construct default CXXFLAGS
#

# Everything is C++
AC_LANG_PUSH(C++)
# save clean settings
CFLAGS_system="$CFLAGS"
CXXFLAGS_system=$CXXFLAGS
CPPFLAGS_system=$CPPFLAGS
LDFLAGS_system=$LDFLAGS
LIBS_system=$LIBS

>conftest.cpp
ZPHPCXXFLAGS=""
AS_IF([$CXX -Wno-narrowing -c conftest.cpp], [ZPHPCXXFLAGS="-Wno-narrowing"])
rm -f conftest.*
AC_SUBST([ZPHPCXXFLAGS])

# default to debug mode
# disable write-strings: too much complains about "" being 'cast' to char* (since const char* is not in the declarations) on g++-4.2 and higher
ZCPPFLAGS="$ZCPPFLAGS \
	-I\${top_builddir}/common/include \
	-I\${top_srcdir}/common/include \
	-I\${top_srcdir}/m4lcommon/include \
	-I\${top_srcdir}/mapi4linux/include \
	-I\${top_srcdir}/provider/common/include \
	-I\${top_srcdir}/provider/libserver/include \
	-I\${top_srcdir}/ECtools/archiver/include \
	-I\${top_srcdir}/inetmapi/include \
	-I\${top_srcdir}/swig/include \
	-I\${top_srcdir}/swig/python/include \
	-I\${top_srcdir}"
ZCXXFLAGS="-Wall -Wmissing-declarations -Wredundant-decls"

# programs that use files >2GB need 64-bit file support
LFS_CFLAGS=`getconf LFS_CFLAGS`
ZCPPFLAGS="$ZCPPFLAGS $LFS_CFLAGS"

m4_define([CXX17TEST], [AC_LANG_SOURCE([
	#if __cplusplus < 201700L
	#error
	#endif
])])
m4_define([CXX14TEST], [AC_LANG_SOURCE([
	#if __cplusplus < 201400L
	#error
	#endif
])])
AH_TEMPLATE([KC_USES_CXX17], [Whether C++17 features were used to build KC])
AC_MSG_CHECKING([C++ standard])
dnl Maybe the default -std= is already enough, then don't potentially downgrade
AC_COMPILE_IFELSE([CXX17TEST], [
	AC_DEFINE([KC_USES_CXX17], [1])
	AC_MSG_RESULT([default is already C++17 or better])
], [
	dnl The user may have specified a -std, honor it for COMPILE_IFELSE
	CXXFLAGS="-std=c++17 $CXXFLAGS_system"
	AC_COMPILE_IFELSE([CXX17TEST], [
		ZCXXFLAGS="$ZCXXFLAGS -std=c++17"
		AC_DEFINE([KC_USES_CXX17], [1])
		AC_MSG_RESULT([selected C++17])
	], [
		CXXFLAGS="-std=c++14 $CXXFLAGS_system"
		AC_COMPILE_IFELSE([CXX14TEST], [
			ZCXXFLAGS="$ZCXXFLAGS -std=c++14"
			AC_MSG_RESULT([selected C++14])
		], [
			AC_MSG_ERROR([at least C++14 is needed])
		])
	])
])
CXXFLAGS="$CXXFLAGS_system"

# disable variadic-macros: too much warning: anonymous variadic macros were introduced in C99, only on new compilers
AC_MSG_CHECKING([whether C++ compiler understands -Wno-variadic-macros option])
if ( echo 'int main() {}' | $CXX -Wno-variadic-macros -E - >/dev/null 2>&1 ); then
   AC_MSG_RESULT([yes])
   ZCXXFLAGS="$ZCXXFLAGS -Wno-variadic-macros"
else
   AC_MSG_RESULT([no])
fi

#
# enable/disable options
#

AC_ARG_ENABLE([release], [AS_HELP_STRING([--disable-release], [Deactivate workarounds for KC-937 and KC-930])],
	[want_release=${enableval}], [want_release=yes])
AS_IF([test "$want_release" = "yes"], [
    ZCPPFLAGS="$ZCPPFLAGS -DNDEBUG"
    ZCXXFLAGS="$ZCXXFLAGS -fno-strict-aliasing"
])

ZCPPFLAGS="$ZCPPFLAGS -DUNICODE"
SWIG_FLAGS="-DUNICODE "
AC_SUBST([SWIG_FLAGS])

dnl These are the only two I will allow to edit LIBS
AC_ARG_WITH([asan], AS_HELP_STRING([--with-asan], [Activate Address Sanitizer]),
	[ZCXXFLAGS="$ZCXXFLAGS -fsanitize=address"])
dnl Cannot reliably mix hidden visibility with UBSAN currently
dnl [https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80963]
NO_VSYM=""
AC_ARG_WITH([ubsan], AS_HELP_STRING([--with-ubsan], [Activate Undefined Behavior Sanitizer]),
	[ZCXXFLAGS="$ZCXXFLAGS -fsanitize=undefined"; VISCXXFLAGS=""; NO_VSYM=1],
	[ZCXXFLAGS="$ZCXXFLAGS -fvisibility=hidden -fvisibility-inlines-hidden"])
AC_SUBST([NO_VSYM])

# profiling
AC_ARG_ENABLE(profiling,
  AS_HELP_STRING([--enable-profiling], [enable profiling (-pg)]),
    [AS_IF([test "$enableval" = "yes"], [
		ZCXXFLAGS="$ZCXXFLAGS -pg"
		ZLDFLAGS="$ZLDFLAGS -pg"
	])
    ])

AC_ARG_ENABLE(coverage,
  AS_HELP_STRING([--enable-coverage], [enable coverage (--coverage)]),
    [AS_IF([test "$enableval" = "yes"], [
		ZCXXFLAGS="$ZCXXFLAGS -g --coverage -fprofile-arcs -ftest-coverage"
		ZLDFLAGS="$ZLDFLAGS -lgcov"
	  ])
    ])

# stats client requires ncurses(w) for --top
# check wide version before non-wide version
AC_DEFUN([LIBNCURSES_WITH_CONFIG],[
	AC_CHECK_PROGS([NCURSES_CONFIG], [ncursesw6-config ncursesw5-config ncurses6-config ncurses5-config ncursesw-config ncurses-config], [])
	AS_IF([test -n "$NCURSES_CONFIG"],
		[
			NCURSES_FLAGS=$("$NCURSES_CONFIG" --cflags)
			NCURSES_LIBS=$("$NCURSES_CONFIG" --libs)
		],
		[
			dnl just pick the lowest common denominator
			NCURSES_FLAGS=""
			NCURSES_LIBS="-lncurses -ltinfo"
		]
	)
	AC_CHECK_HEADERS([ncurses.h])
	LIBS="$LIBS_system"
	CFLAGS="$CFLAGS_system"
	CXXFLAGS="$CXXFLAGS_system"
	AC_SUBST([NCURSES_FLAGS])
	AC_SUBST([NCURSES_LIBS])
])
LIBNCURSES_WITH_CONFIG

AC_CHECK_HEADERS([syslog.h sys/random.h malloc.h])
AC_CHECK_FUNCS([arc4random_buf mallinfo timegm getpeereid getpwnam_r getrandom vsyslog vsnprintf_l])
AC_CHECK_MEMBERS([siginfo_t.si_fd siginfo_t.si_utime], [], [], [#include <csignal>])
AC_CHECK_MEMBERS([sockpeercred.uid], [], [], [#include <sys/socket.h>])
AH_TEMPLATE([KC_USES_TIMEGM], [the system has timegm])
AS_IF([test "$ac_cv_func_timegm" = yes], [AC_DEFINE([KC_USES_TIMEGM], [1])])

#
# find required/optional libraries
#

AC_ARG_WITH([php], [AS_HELP_STRING([--without-php], [Disable building PHP components])])
AS_IF([test "$with_php" != "no"], [
	# Checks for PHP through php-config and tests the truth of
	# php-config results
	PHP_WITH_PHP_CONFIG
	PHP_CHECK_INCLUDES
	echo "PHP version: $PHP_VERSION"
	AS_CASE([$PHP_VERSION],
		[5.*], [AC_MSG_ERROR([php5 is not supported])],
		[7.*], [with_php=7]
	)
])
PKG_CHECK_MODULES([libHX], [libHX >= 1.10])
AH_TEMPLATE([HAVE_IDN2_H], [libidn present])
PKG_CHECK_MODULES([idn], [libidn2], [AC_DEFINE([HAVE_IDN2_H], [1])], [:])
PKG_CHECK_MODULES([xapian], [xapian-core])
PKG_CHECK_MODULES([XML2], [libxml-2.0])

dbcxx_CPPFLAGS=""
dbcxx_LIBS="-ldb_cxx"
CPPFLAGS="$CPPFLAGS_system $dbcxx_CPPFLAGS"
AC_CHECK_HEADERS([db_cxx.h], [], [AC_MSG_ERROR([Berkeley DB C++ interface required])])
CPPFLAGS="$CPPFLAGS_system"
AC_SUBST([dbcxx_CPPFLAGS])
AC_SUBST([dbcxx_LIBS])

# Checks for MySQL 4.1 or better
AC_DEFUN([MYSQL_WITH_MYSQL_CONFIG],[
  AC_PATH_PROGS([MYSQL_CONFIG], [mysql_config mariadb_config])
  AC_ARG_WITH([mysql-config], AS_HELP_STRING([--with-mysql-config=PATH], [path to the mysql-config script of mysql]),
    [MYSQL_CONFIG="$withval"])
  AS_IF([test -z "$MYSQL_CONFIG"], [AC_MSG_ERROR([mysql config program not found])])
  MYSQL_INCLUDES=`$MYSQL_CONFIG --include`
  MYSQL_LIBS=`$MYSQL_CONFIG --libs`
  MYSQL_VERSION=`$MYSQL_CONFIG --version` # see if it's there

  if test -z "$MYSQL_VERSION"; then
    AC_MSG_ERROR([Cannot find a mysql installation. Please use --with-mysql-config=PATH or install libmysqlclient-dev.])
  fi

  AC_SUBST(MYSQL_INCLUDES)
  AC_SUBST(MYSQL_LIBS)
  AC_SUBST(MYSQL_VERSION)

  AC_MSG_CHECKING([for MySQL includes])
  AC_MSG_RESULT($MYSQL_INCLUDES)
  AC_MSG_CHECKING([for MySQL libs])
  AC_MSG_RESULT($MYSQL_LIBS)
])
MYSQL_WITH_MYSQL_CONFIG

CXXFLAGS="$CXXFLAGS $MYSQL_INCLUDES"
LIBS="$LIBS $MYSQL_LIBS"
dnl get_option: mysql 5.7.0
dnl set_character_set: mysql 5.0.7
AC_CHECK_FUNCS([mysql_get_option mysql_set_character_set])
LIBS=$LIBS_system
CXXFLAGS=$CXXFLAGS_system

#---------------------------------------------
# pthread setname (4 non-portable variants...)
#---------------------------------------------
LIBS="-pthread $LIBS"
AC_CHECK_HEADERS([pthread_np.h])
define(pthread_np_preamble,[
  #include <pthread.h>
  #if HAVE_PTHREAD_NP_H
  #  include <pthread_np.h>
  #endif
])
# 2-arg setname (e.g. Linux/glibc, QNX, IBM)
AC_MSG_CHECKING([for 2-arg pthread_setname_np])
AC_LINK_IFELSE([AC_LANG_PROGRAM(pthread_np_preamble, [
    pthread_setname_np(pthread_self(), "foo")
])], [
  AC_DEFINE(HAVE_PTHREAD_SETNAME_NP_2, 1, [2-arg pthread_setname_np])
  AC_MSG_RESULT([yes])
], [
  AC_MSG_RESULT([no])

  # 2-arg set_name (e.g. FreeBSD, OpenBSD)
  AC_MSG_CHECKING([for 2-arg pthread_set_name_np])
  AC_LINK_IFELSE([AC_LANG_PROGRAM(pthread_np_preamble, [
      return pthread_set_name_np(pthread_self(), "foo");
  ])], [
    AC_DEFINE(HAVE_PTHREAD_SET_NAME_NP_2, 1, [2-arg pthread_set_name_np])
    AC_MSG_RESULT([yes])
  ], [
    AC_MSG_RESULT([no])

    # 1-arg setname (e.g. Darwin)
    AC_MSG_CHECKING([for 1-arg pthread_setname_np])
    AC_LINK_IFELSE([AC_LANG_PROGRAM(pthread_np_preamble, [
        return pthread_setname_np("foo");
    ])], [
      AC_DEFINE(HAVE_PTHREAD_SETNAME_NP_1, 1, [1-arg pthread_setname_np])
      AC_MSG_RESULT([yes])
    ], [
      AC_MSG_RESULT([no])

      # 3-arg setname (e.g. NetBSD)
      AC_MSG_CHECKING([for 3-arg pthread_setname_np])
      AC_LINK_IFELSE([AC_LANG_PROGRAM(pthread_np_preamble, [
          return pthread_setname_np(pthread_self(), "foo", NULL);
      ])], [
        AC_DEFINE(HAVE_PTHREAD_SETNAME_NP_3, 1, [3-arg pthread_setname_np])
        AC_MSG_RESULT([yes])
      ], [
        AC_MSG_RESULT([no])
      ])
    ])
  ])
])
#---------------------------------------------
# end pthread_setname stuff
#---------------------------------------------

CXXFLAGS="$CXXFLAGS_system $ZCXXFLAGS"
LDFLAGS="$LDFLAGS_system $ZLDFLAGS"
AC_CHECK_FUNCS([pthread_getname_np])

dnl Broken .pc file until (not including) 2.8.83
PKG_CHECK_MODULES([GSOAP], [gsoapssl++ >= 2.8])
AC_COMPILE_IFELSE([AC_LANG_SOURCE([
	#include <stdsoap2.h>
	#if GSOAP_VERSION < 20873
	#	error too old
	#endif
])], [], [AC_MSG_ERROR([Need gsoap >= 2.8.73])])
PKG_CHECK_MODULES([ICAL], [libical >= 0.42])
AH_TEMPLATE([HAVE_CURL_CURL_H], [curl present])
PKG_CHECK_MODULES([curl], [libcurl >= 7], [AC_DEFINE([HAVE_CURL_CURL_H], [1])], [:])
PKG_CHECK_MODULES([rrd], [librrd >= 1.3], [], [:])
PKG_CHECK_MODULES([TCMALLOC], [libtcmalloc_minimal], [], [:])
CPPFLAGS="$CPPFLAGS $TCMALLOC_CFLAGS"
AC_CHECK_HEADERS([gperftools/malloc_extension.h google/malloc_extension.h])
CPPFLAGS="$CPPFLAGS_system"
PKG_CHECK_MODULES([VMIME], [vmime >= 0.9.2k1])
PKG_CHECK_MODULES([jsoncpp], [jsoncpp >= 1.4.0], [], [
	PKG_CHECK_MODULES([jsoncpp], [jsoncpp >= 0.8 jsoncpp < 1])
])


#
# Check for user plugin required libraries
#

LDAP_FLAGS=""
LDAP_LIBS=""
AC_CHECK_HEADERS([ldap.h], [
	LDAP_FLAGS="-DLDAP_DEPRECATED=1"
	LDAP_LIBS="-lldap_r"
	want_ldap=yes
])
AC_SUBST(LDAP_LIBS)
AC_SUBST(LDAP_FLAGS)

LIBS="$LIBS $LDAP_LIBS"
CXXFLAGS="$CXXFLAGS $LDAP_FLAGS"
AC_CHECK_FUNCS([ldap_start_tls_s])
AC_CHECK_FUNCS([ldap_create_page_control])
LIBS=$LIBS_system
CXXFLAGS=$CXXFLAGS_system

AC_CHECK_HEADERS([shadow.h], [want_unix=yes], [want_unix=no])

AC_MSG_CHECKING([for iconv])
AC_LINK_IFELSE([ AC_LANG_SOURCE([
#include <stdlib.h>
#include <iconv.h>
int main() {
iconv_t i; i = iconv_open("", "");
}]) ],
	[ AC_MSG_RESULT([yes]) ],
	[ AC_MSG_RESULT([no])
	  AC_MSG_CHECKING([for iconv in -liconv])
	  LIBS="$LIBS -liconv"
	  AC_LINK_IFELSE([ AC_LANG_SOURCE([
#include <stdlib.h>
#include <iconv.h>
int main() {
iconv_t i; i = iconv_open("", "");
}]) ],
		[ AC_MSG_RESULT([yes])
		  ICONV_LIBS=$LIBS ],
		[ AC_MSG_RESULT([no])
		  AC_MSG_ERROR([required library libiconv missing or unusable]) ]) ])
AC_SUBST(ICONV_LIBS)
LIBS=$LIBS_system

AC_SEARCH_LIBS([crypt_r], [crypt], [
	AS_IF([test "$LIBS" = "$LIBS_system"], [CRYPT_LIBS=""], [CRYPT_LIBS="-lcrypt"])
])
LIBS="$LIBS_system"
AC_SEARCH_LIBS([clock_gettime], [rt], [
	AS_IF([test "$LIBS" = "$LIBS_system"], [clock_LIBS=""], [clock_LIBS="-lrt"])
])
LIBS="$LIBS_system"
AC_SUBST([CRYPT_LIBS])
AC_SUBST([clock_LIBS])
PKG_CHECK_EXISTS([libssl],
	[
		PKG_CHECK_MODULES([SSL], [libssl], [], [ssl_LIBS=-lssl])
	],
	[
		AC_CHECK_LIB([ssl], [SSL_accept], [SSL_LIBS="-lssl"], AC_MSG_ERROR([required library libssl missing or unusable]))
		AC_SUBST([SSL_LIBS])
	])
PKG_CHECK_EXISTS([libcrypto],
	[
		PKG_CHECK_MODULES([CRYPTO], [libcrypto])
	],
	[
		AC_CHECK_LIB([crypto], [BIO_accept], [CRYPTO_LIBS="-lcrypto"], AC_MSG_ERROR([required library libcrypto missing or unusable]))
		AC_SUBST([CRYPTO_LIBS])
	])
ZCPPFLAGS="$ZCPPFLAGS $CRYPTO_CFLAGS"
LIBS=$LIBS_system

# OpenSSL check for new EVP_PKEY_cmp() function
LIBS="$LIBS $CRYPTO_LIBS"
CPPFLAGS="$CPPFLAGS $CRYPTO_CFLAGS"
AC_CHECK_FUNCS([EVP_PKEY_cmp])
LIBS=$LIBS_system
CPPFLAGS=$CPPFLAGS_system

# Check for pam library (optional)
PAM_LIBS=
AC_CHECK_LIB([pam], [pam_start],
		[PAM_LIBS="-lpam"]
		AC_DEFINE(HAVE_PAM, [1], [pam authentication available]),
		AC_MSG_RESULT([library libpam missing or unusable]))
AC_SUBST(PAM_LIBS)

# Check for kerberos library (optional)
AC_DEFUN([LIBKRB5_WITH_KRB5_CONFIG],[
  AC_ARG_WITH([krb5-config], AS_HELP_STRING([--with-krb5-config=PATH], [path to the krb5-config script of libkrb5]),
    [KRB5_CONFIG=${withval}],[KRB5_CONFIG=krb5-config])

  AC_MSG_CHECKING([for krb5-config presence])
  KRB5_CONFIG=`which $KRB5_CONFIG 2>/dev/null`
  AS_IF([test ! -x "$KRB5_CONFIG"], [
    AC_MSG_RESULT([not found])
  ], [
    AC_MSG_RESULT([$KRB5_CONFIG])

    KRB5_CFLAGS=`$KRB5_CONFIG --cflags`
    KRB5_LIBS=`$KRB5_CONFIG --libs gssapi`

	dnl test if found krb5 is good enough version
	CXXFLAGS="$CXXFLAGS $KRB5_CFLAGS"
	LIBS="$LIBS $KRB5_LIBS"

	use_krb=2
	AC_CHECK_FUNCS([gss_acquire_cred], [AC_DEFINE(HAVE_GSSAPI, [1], [Kerberos Single Signon Authentication enabled]) ], [ use_krb=$((use_krb-1)) ])
	AC_CHECK_FUNCS([krb5_free_unparsed_name], [ AC_DEFINE(HAVE_KRB5, [1], [Kerberos Password Authentication enabled]) ], [ use_krb=$((use_krb-1)) ])
	AC_MSG_CHECKING([for krb5 usability])
	AS_IF([test "$use_krb" = 0], [
		KRB5_CFLAGS=
		KRB5_LIBS=
		AC_MSG_RESULT([no])
	], [
		AC_MSG_RESULT([yes])
	])
	LIBS="$LIBS_system"
	CFLAGS="$CFLAGS_system"
	CXXFLAGS="$CXXFLAGS_system"
	AC_SUBST(KRB5_CFLAGS)
	AC_SUBST(KRB5_LIBS)
  ])
])
LIBKRB5_WITH_KRB5_CONFIG

# Check for libs3 amazon s3 availability
LIBS_system="$LIBS"
AC_CHECK_HEADERS([libs3.h])
AC_SEARCH_LIBS([S3_initialize], [s3], [s3_LIBS="-ls3"; LIBS="$LIBS_system"])
AC_SUBST([s3_LIBS])

# Check for kcoidc
LIBS_system="$LIBS"
AC_CHECK_HEADERS([kcoidc.h])
AC_SEARCH_LIBS([kcoidc_initialize], [kcoidc], [kcoidc_LIBS="-lkcoidc"; LIBS="$LIBS_system"])
AC_SUBST([kcoidc_LIBS])

AC_CHECK_FUNCS([uuid_create], UUID_LIBS="",	dnl uuid_create is in libc on FreeBSD
  AC_CHECK_LIB([uuid], [uuid_create],
    UUID_LIBS="-luuid"
    AC_DEFINE(HAVE_UUID_CREATE),	dnl uuid_create is in libuuid on OpenBSD, also define such in config.h
    AC_CHECK_LIB([uuid], [uuid_generate], UUID_LIBS="-luuid", dnl uuid_generate in uuid on linux
      AC_MSG_ERROR([required library libuuid missing or unusable])
    )
  )
)
AC_SUBST(UUID_LIBS)

AC_CHECK_FUNCS([dlopen], DL_LIBS="", dnl dlopen is in libc on FreeBSD/OpenBSD
  AC_CHECK_LIB([dl], [dlopen], DL_LIBS="-ldl", dnl use -ldl on linux
    AC_MSG_ERROR([required library libdl missing or unusable])
  )
)
AC_SUBST(DL_LIBS)

AC_CHECK_LIB([z], [gzopen], GZ_LIBS="-lz",
  AC_MSG_ERROR([required library libz missing or unusable])
)
AC_SUBST(GZ_LIBS)

AC_CHECK_FUNCS([gettext], INTL_LIBS="", dnl gettext in libc
  AC_CHECK_LIB([intl], [gettext], INTL_LIBS="-lintl", dnl in -lintl
    AC_MSG_ERROR([required library libintl missing or unusable])
  )
)
AC_SUBST(INTL_LIBS)

# check epoll support
AC_ARG_ENABLE([epoll], AS_HELP_STRING([--disable-epoll], [disable epoll and fallback to select]), [want_epoll=${enableval}], [want_epoll=yes])
AS_IF([test "$BUILDOS" = linux && test "$want_epoll" = "yes"], [
	AC_CHECK_FUNCS([epoll_create], [ZCPPFLAGS="$ZCPPFLAGS -DUSE_EPOLL"])
])

tidy_CPPFLAGS="-I/usr/include/tidy"
CPPFLAGS="$CPPFLAGS $tidy_CPPFLAGS"
AC_CHECK_HEADERS([tidybuffio.h])
AC_SUBST([tidy_CPPFLAGS])
CPPFLAGS="$CPPFLAGS_system"

dnl The "Locale" class is in icu-uc.
dnl The "CollationKey" class is in icu-i18n.
req_icu_uc="icu-uc"
req_icu_i18n="icu-i18n"
PKG_CHECK_EXISTS([icu-uc], [have_icu=1], [have_icu=0])
PKG_CHECK_EXISTS([icu-i18n], [have_icu=1], [have_icu=0])
AS_IF([test "$have_icu" = 1], [AC_MSG_RESULT([Found ICU with modern pkg-config files])])
AS_IF([test "$have_icu" = 0], [
	dnl RHEL 6 / icu 4.x
	PKG_CHECK_EXISTS([icu], [
		req_icu_uc="icu"
		req_icu_i18n="icu"
		AC_MSG_RESULT([Found ICU with old-style pkg-config files])
	])
])
AS_IF([test "$want_icu" = "auto"], [
	PKG_CHECK_MODULES([icu_uc], [$req_icu_uc], [have_icu=1], [have_icu=0])
	PKG_CHECK_MODULES([icu_i18n], [$req_icu_i18n], [have_icu=1], [have_icu=0])
])
AS_IF([test "$have_icu" = 0], [
	AC_CHECK_HEADERS([unicode/coll.h], [have_icu=2])
])
AS_IF([test "$have_icu" = 1], [
	dnl PKG_CHECK_EXISTS only tests, and never logs.
	dnl Now that we know we want PKG_CHECK, do it for real:
	PKG_CHECK_MODULES([icu_uc], [$req_icu_uc])
	PKG_CHECK_MODULES([icu_i18n], [$req_icu_i18n])
])
AS_IF([test "$have_icu" = 2], [
	AC_MSG_RESULT([Found ICU by header file])
	icu_uc_CFLAGS=""
	icu_i18n_CFLAGS=""
	icu_uc_LIBS="-licuuc"
	icu_i18n_LIBS="-licui18n"
	AC_SUBST([icu_uc_LIBS])
	AC_SUBST([icu_i18n_LIBS])
])
AS_IF([test "$have_icu" = 0], [
	AC_MSG_ERROR([ICU is required but not found])
])
ZCPPFLAGS="$ZCPPFLAGS $icu_uc_CFLAGS $icu_i18n_CFLAGS"

# done testing C++ requirements
AC_LANG_POP
dnl Set default project flags. On overriding AM_CXXFLAGS, include ${ZCXXFLAGS}
AM_CXXFLAGS="$ZCXXFLAGS"
AM_LDFLAGS="$ZLDFLAGS"
AC_SUBST([ZCPPFLAGS])
AC_SUBST([ZCXXFLAGS])
AC_SUBST([AM_CXXFLAGS])
AC_SUBST([AM_LDFLAGS])

AC_ARG_ENABLE([pybind], [AS_HELP_STRING([--disable-pybind],
	[Enables/disables building of C++↔Python bindings (default: auto-enable)])],
	[want_pybind="$enableval"], [want_pybind=auto])
AS_IF([test "$want_pybind" != no], [
	PKG_CHECK_MODULES([PYTHON], [$PYTHON_PC], [], [:])
	AS_IF([test -n "$PYTHON_LIBS"], [
		AS_IF([test -z "$SWIG_EXEC"], [AC_MSG_ERROR([swig is required for Python])])
	], [test "$want_python" = yes], [
		AC_MSG_ERROR([python requested but not satisfiable])
	])
])

]) dnl enable-cxx

AM_CONDITIONAL([ENABLE_BASE], [test "$enable_base" != no])
AM_CONDITIONAL([ENABLE_PYBIND], [test "$want_pybind" != no -a -n "$PYTHON_LIBS"])
AM_CONDITIONAL([HAVE_MHTTPD], [test -n "$mhttpd_CFLAGS" -o -n "$mhttpd_LIBS"])
AM_CONDITIONAL([HAVE_RRD], [test -n "$rrd_CFLAGS" -o -n "$rrd_LIBS"])
AM_CONDITIONAL([HAVE_TIDY], [test "x$ac_cv_header_tidybuffio_h" = "xyes"])
AM_CONDITIONAL([WITH_EPOLL], [test "$want_epoll" = "yes"])
AM_CONDITIONAL([WITH_KRB5], [test -n "$KRB5_LIBS"])
AM_CONDITIONAL([WITH_LDAP], [test "$want_ldap" = yes])
AM_CONDITIONAL([WITH_PAM], [test -n "$PAM_LIBS"])
AM_CONDITIONAL([WITH_PHP7], [test "$with_php" = 7])
AM_CONDITIONAL([WITH_UNIX], [test "$want_unix" = yes])
AS_IF([test -n "$PYTHON_LIBS" || test -n "$PYTHON"], [AM_PATH_PYTHON([2.5])])

#
# Output files
#
AC_CONFIG_FILES([Makefile
		mapi4linux/include/Makefile
		m4lcommon/include/kopano/Makefile
		provider/include/Makefile
		inetmapi/include/inetmapi/Makefile
		spooler/python/Makefile
		spooler/python/plugins/Makefile
		common/include/kopano/Makefile
		ECtools/archiver/include/kopano/Makefile
		ECtools/backup/Makefile
		ECtools/cli/Makefile
		ECtools/migration-pst/Makefile
		ECtools/presence/Makefile
		ECtools/search/Makefile
		ECtools/spamd/Makefile
		ECtools/utils/Makefile
		installer/Makefile
		installer/linux/Makefile
		installer/userscripts/Makefile
		installer/quotamail/Makefile
		installer/searchscripts/Makefile
		po/Makefile
		swig/Makefile
		swig/python/Makefile swig/python/python.sym
		swig/python/kopano/Makefile
		common/kopano.pc common/libkcutil.sym
		default.sym
		gateway/doveauth.sym gateway/dovestore.sym
		mapi4linux/src/libmapi.sym
		provider/plugin.sym provider/provider.sym
		provider/libkcserver.sym provider/libkcsoap.sym
		spooler/libkcpyplug.sym
])

# Python tools

AC_CONFIG_FILES([ECtools/search/kopano-search], [chmod +x ECtools/search/kopano-search])
AC_CONFIG_FILES([ECtools/backup/kopano-backup], [chmod +x ECtools/backup/kopano-backup])
AC_CONFIG_FILES([ECtools/cli/kopano-cli], [chmod +x ECtools/cli/kopano-cli])
AC_CONFIG_FILES([ECtools/rules/kopano-rules], [chmod +x ECtools/rules/kopano-rules])
AC_CONFIG_FILES([ECtools/presence/kopano-presence], [chmod +x ECtools/presence/kopano-presence])
AC_CONFIG_FILES([ECtools/migration-pst/kopano-migration-pst], [chmod +x ECtools/migration-pst/kopano-migration-pst])
AC_CONFIG_FILES([ECtools/spamd/kopano-spamd], [chmod +x ECtools/spamd/kopano-spamd])
AC_CONFIG_FILES([ECtools/utils/kopano-mr-accept], [chmod +x ECtools/utils/kopano-mr-accept])
AC_CONFIG_FILES([ECtools/utils/kopano-mr-process], [chmod +x ECtools/utils/kopano-mr-process])
AC_CONFIG_FILES([ECtools/utils/kopano-autorespond], [chmod +x ECtools/utils/kopano-autorespond])
AC_CONFIG_FILES([ECtools/search/kopano_search/version.py], [])
AC_CONFIG_FILES([ECtools/backup/kopano_backup/version.py], [])
AC_CONFIG_FILES([ECtools/cli/kopano_cli/version.py], [])
AC_CONFIG_FILES([ECtools/rules/kopano_rules/version.py], [])
AC_CONFIG_FILES([ECtools/presence/kopano_presence/version.py], [])
AC_CONFIG_FILES([ECtools/migration-pst/kopano_migration_pst/version.py], [])
AC_CONFIG_FILES([ECtools/spamd/kopano_spamd/version.py], [])
AC_CONFIG_FILES([ECtools/utils/kopano_utils/version.py], [])
AC_CONFIG_FILES([swig/python/kopano/kopano/version.py], [])
AC_CONFIG_FILES([swig/python/MAPI/version.py], [])

AC_OUTPUT
$LN_TFSV "$abs_top_srcdir" "@"
