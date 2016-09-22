# -*- Makefile -*-

SUBDIRS=python

AM_CPPFLAGS = ${ZCPPFLAGS} $(DEBUGFLAGS) $(BOOST_CPPFLAGS) $(PYTHON_INCLUDES) \
	-DSWIG_TYPE_TABLE=MAPI \
	-I${top_srcdir}/mapi4linux/include \
	-I${top_srcdir}/inetmapi -I${top_srcdir}/provider/common \
	-I${top_srcdir}/common -I${top_srcdir}/m4lcommon \
	-I${top_srcdir}/libfreebusy \
	-I${top_srcdir}/ECtools/archiver

sbin_PROGRAMS = kopano-dagent kopano-spooler
noinst_DATA = kopano-dagent.ldx kopano-spooler.ldx
CLEANFILES = ${noinst_DATA}

kopano_dagent_LDADD = ${top_builddir}/inetmapi/libkcinetmapi.la \
	${top_builddir}/mapi4linux/src/libmapi.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/common/libkcutil.la \
	${top_builddir}/common/libkcssl.la \
	${top_builddir}/ECtools/libkcarchiver.la \
	${top_builddir}/ECtools/libkcarchivercore.la \
	$(PROG_LIBS) $(SSL_LIBS) $(PYTHON_LIBS) $(XML2_LIBS) ${icu_uc_LIBS}

kopano_spooler_LDADD = ${top_builddir}/inetmapi/libkcinetmapi.la \
	${top_builddir}/mapi4linux/src/libmapi.la \
	${top_builddir}/common/libkcmapi.la \
	${top_builddir}/common/libkcutil.la \
	${top_builddir}/common/libkcssl.la \
	${top_builddir}/ECtools/libkcarchiver.la \
	${top_builddir}/ECtools/libkcarchivercore.la \
	$(PROG_LIBS) $(PYTHON_LIBS) $(XML2_LIBS)

kopano_dagent_SOURCES = \
	DAgent.cpp spmain.h rules.cpp rules.h LMTP.cpp LMTP.h \
	archive.cpp archive.h PyMapiPlugin.cpp PyMapiPlugin.h \
	PythonSWIGRuntime.h
kopano_spooler_SOURCES = \
	Spooler.cpp spmain.h mailer.cpp mailer.h archive.cpp archive.h \
	PyMapiPlugin.cpp PyMapiPlugin.h PythonSWIGRuntime.h

BUILT_SOURCES=PythonSWIGRuntime.h
CLEANFILES += PythonSWIGRuntime.h
EXTRA_DIST=PythonSWIGRuntime.h

PythonSWIGRuntime.h:
	$(SWIG_EXEC) -python -external-runtime PythonSWIGRuntime.h

check-syntax:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
		-fsyntax-only -fmessage-length=0 ${CHK_SOURCES} -Wall -Wformat=2

include ../global.am
