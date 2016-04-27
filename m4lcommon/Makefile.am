# -*- Makefile -*-

SUBDIRS = include/kopano

AM_CPPFLAGS = ${ZCPPFLAGS} $(DEBUGFLAGS) $(BOOST_CPPFLAGS) \
	-DMAPICONFIGDIR=\"${MAPICONFIGDIR}\" \
	-DPKGLIBDIR=\"${pkglibdir}\" \
	-I$(top_srcdir)/mapi4linux/include -I$(top_srcdir)/provider/common \
	-I${top_srcdir}/common -I${top_srcdir}/m4lcommon \
	-I${top_srcdir}/libfreebusy

noinst_LTLIBRARIES = libm4lcommon.la

m4lincludedir = ${includedir}/mapi4linux

m4linclude_HEADERS = include/edkguid.h
noinst_HEADERS = \
	ECFeatureList.h \
	ECInvariantChecker.h \
	ECIterators.h \
	ECMAPIDebug.h \
	HrException.h \
	IECChangeAdviseSink.h \
	IECChangeAdvisor.h \
	IECExchangeModifyTable.h \
	IECExportAddressbookChanges.h \
	IECExportChanges.h \
	IECImportAddressbookChanges.h \
	IECImportContentsChanges.h \
	IECImportHierarchyChanges.h \
	IECMultiStoreTable.h \
	IECSpooler.h \
	IECTestProtocol.h \
	IMAPIOffline.h \
	MAPIConsoleTable.h \
	MAPINotifSink.h \
	favoritesutil.h \
	mapicontact.h
# These were split off libcommon_mapi
libm4lcommon_la_SOURCES = \
	CommonUtil.cpp ECIterators.cpp \
	ECMAPIDebug.cpp ECMemTable.cpp \
	MAPIConsoleTable.cpp \
	MAPINotifSink.cpp Util.cpp \
	favoritesutil.cpp
libm4lcommon_la_LIBADD = \
	../common/libkcutil.la ../common/libkcmapi.la \
	-lpthread


noinst_HEADERS += \
	ECFeatures.h
# These were split off libcommon_util
libm4lcommon_la_SOURCES += \
	ECFeatures.cpp ECRestriction.cpp \
	RecurrenceState.cpp userutil.cpp

check-syntax:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
		-fsyntax-only -fmessage-length=0 ${CHK_SOURCES} -Wall -Wformat=2

include ../global.am
