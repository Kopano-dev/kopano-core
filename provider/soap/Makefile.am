# -*- Makefile -*-

AM_CPPFLAGS = ${ZCPPFLAGS} -I${top_srcdir}/mapi4linux/include \
              -I${top_srcdir}/provider/include ${GSOAP_CFLAGS}
AM_CXXFLAGS = ${ZCXXFLAGS} -Wno-sign-compare

lib_LTLIBRARIES = libkcsoapserver.la libkcsoapclient.la
#noinst_DATA = libkcsoapserver.ldd libkcsoapclient.ldd

nodist_libkcsoapserver_la_SOURCES = soapC.cpp soapServer.cpp
libkcsoapserver_la_LIBADD = ${GSOAP_LIBS}
libkcsoapserver_la_LDFLAGS = ${AM_LDFLAGS} \
	-Wl,--version-script=${top_builddir}/default.sym
EXTRA_libkcsoapserver_la_DEPENDENCIES = ${top_builddir}/default.sym

nodist_libkcsoapclient_la_SOURCES = soapC.cpp soapClient.cpp
libkcsoapclient_la_LIBADD = ${GSOAP_LIBS}
libkcsoapclient_la_LDFLAGS = ${AM_LDFLAGS} \
	-Wl,--version-script=${top_builddir}/default.sym
EXTRA_libkcsoapclient_la_DEPENDENCIES = ${top_builddir}/default.sym

SOAPHEADER = ${top_srcdir}/provider/include/proto.h
SOAP_INC = ${top_srcdir}/provider/include/
SOAP_NS = KCmd.nsmap KCmd.wsdl

SOAP_H_FILES = soapH.h soapStub.h
SOAP_CPP_CLIENT = soapClient.cpp soapC.cpp
SOAP_LIB_FILES = soapClientLib.cpp soapServerLib.cpp soapKCmdObject.h
SOAP_CPP_SRC=$(SOAP_CPP_CLIENT) soapServer.cpp $(SOAP_H_FILES) $(SOAP_NS) $(SOAP_LIB_FILES)

$(SOAP_CPP_SRC) : ns.xsd
ns.xsd: $(SOAPHEADER)
	$(GSOAP_SOAPCPP2) -x -I$(SOAP_INC) $<

#libsoapserver_a_LDADD=$(SOAP_CPP_LIB)
BUILT_SOURCES=ns.xsd $(SOAP_CPP_SRC)
CLEANFILES=$(SOAP_CPP_SRC) *.req.xml *.res.xml *.wsdl *.nsmap *.xsd *Proxy\.h *~

check-syntax:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
		-fsyntax-only -fmessage-length=0 ${CHK_SOURCES} -Wall -Wformat=2

include ../../global.am
