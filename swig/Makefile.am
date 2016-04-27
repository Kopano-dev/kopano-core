# make sure common swig lib builds first

SUBDIRS = . include/kopano python

AM_CPPFLAGS = ${ZCPPFLAGS} $(DEBUGFLAGS) \
	-I$(top_srcdir)/mapi4linux/include -I$(top_srcdir)/provider/common \
	-I${top_srcdir}/common -I${top_srcdir}/m4lcommon

EXTRA_DIST = edkmdb.i mapi.i mapiutil.i mapidefs.i mapicode.i mapix.i	\
	mapinotifsink.i typemap.i helpers.i IECExportChanges.i				\
	IECMultiStoreTable.i IECServiceAdmin.i IECSpooler.i					\
	IECTestProtocol.i IECLicense.i inetmapi.i icalmapi.i				\
	swig_iunknown.h ecdefs.i libcommon.i archiver.i licenseclient.i		\
	libfreebusy.i kopanosync.i ECLogger.i RecurrenceState.i				\
	RecurrenceState.swig.h

check-syntax:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
		-fsyntax-only -fmessage-length=0 ${CHK_SOURCES} -Wall -Wformat=2
