# -*- Makefile -*-

SWIG_CXXFLAGS = -Wall -python -c++ -threads \
	-I${top_srcdir}/common/include -I${top_srcdir}/swig/include \
	-I${srcdir}/include

lib_LTLIBRARIES = libkcpyconv.la libkcpydirector.la
pyexec_LTLIBRARIES = _MAPICore.la _inetmapi.la _icalmapi.la _libfreebusy.la
noinst_LTLIBRARIES = _archiver.la _RecurrenceState.la
noinst_DATA = libkcpyconv.ldd libkcpydirector.ldd \
	_MAPICore.ldd _inetmapi.ldd _icalmapi.ldd \
	_archiver.ldd _libfreebusy.ldd _RecurrenceState.ldd
CLEANFILES = ${noinst_DATA} .*.dep

# disable warnings since code is mostly generated
AM_CPPFLAGS = ${ZCPPFLAGS} \
	-I${top_srcdir}/mapi4linux/include -I${top_srcdir}/provider/common \
	-I${top_srcdir}/common -I${top_srcdir}/m4lcommon \
	${PYTHON_CFLAGS} -I${top_srcdir}/swig -w \
	-DSWIG_PYTHON_OUTPUT_TUPLE -DSWIG_TYPE_TABLE=MAPI
AM_CXXFLAGS = ${ZCXXFLAGS} -fvisibility=default
python_SYFLAGS = -Wl,--version-script=python.sym

libkcpyconv_la_SOURCES = conversion.cpp
libkcpyconv_la_LDFLAGS = ${AM_LDFLAGS} ${python_SYFLAGS${NO_VSYM}} \
	-release ${PYTHON_VERSION}
libkcpyconv_la_LIBADD  = ${PYTHON_LIBS} \
	${top_builddir}/libmapi.la
EXTRA_libkcpyconv_la_DEPENDENCIES = python.sym

libkcpydirector_la_SOURCES = ../director_util.cpp
libkcpydirector_la_LDFLAGS = ${AM_LDFLAGS} ${python_SYFLAGS${NO_VSYM}} \
	-release ${PYTHON_VERSION}
libkcpydirector_la_LIBADD  = -lpthread
EXTRA_libkcpydirector_la_DEPENDENCIES = python.sym

_MAPICore_la_LDFLAGS = ${AM_LDFLAGS} -module -avoid-version
_MAPICore_la_LIBADD = libkcpyconv.la libkcpydirector.la \
	${top_builddir}/libmapi.la \
	${top_builddir}/libkcutil.la \
	$(XML2_LIBS) $(PYTHON_LIBS)

_inetmapi_la_CPPFLAGS = ${AM_CPPFLAGS} -I${top_srcdir}/inetmapi
_inetmapi_la_LDFLAGS = ${AM_LDFLAGS} -module -avoid-version
_inetmapi_la_LIBADD = libkcpyconv.la \
	${top_builddir}/libmapi.la \
	${top_builddir}/libkcutil.la \
	${top_builddir}/libkcinetmapi.la \
	$(PYTHON_LIBS)

_icalmapi_la_CPPFLAGS = ${AM_CPPFLAGS} -I${top_srcdir}/libicalmapi \
	-I${top_srcdir}/libfreebusy
_icalmapi_la_LDFLAGS = ${AM_LDFLAGS} -module -avoid-version
_icalmapi_la_LIBADD = libkcpyconv.la \
	${top_builddir}/libmapi.la \
	${top_builddir}/libkcicalmapi.la \
	${top_builddir}/libkcutil.la \
	${top_builddir}/libkcfreebusy.la \
	$(ICAL_LIBS) $(PYTHON_LIBS)

_archiver_la_CPPFLAGS = ${AM_CPPFLAGS} -I${top_srcdir}/ECtools/archiver
_archiver_la_LDFLAGS = ${AM_LDFLAGS} -module -avoid-version -rpath '$(libdir)'
_archiver_la_LIBADD = ${top_builddir}/libkcarchiver.la ${PYTHON_LIBS}

_RecurrenceState_la_LDFLAGS = ${AM_LDFLAGS} -module -avoid-version -rpath '$(libdir)'
_RecurrenceState_la_LIBADD = libkcpyconv.la \
	${top_builddir}/libmapi.la \
	${top_builddir}/libkcutil.la \
	$(PYTHON_LIBS)

_libfreebusy_la_CPPFLAGS = ${AM_CPPFLAGS} -I${top_srcdir}/libfreebusy
_libfreebusy_la_LDFLAGS = ${AM_LDFLAGS} -module -avoid-version
_libfreebusy_la_LIBADD = libkcpyconv.la ${top_builddir}/libmapi.la \
	${top_builddir}/libkcfreebusy.la \
	${top_builddir}/libkcutil.la \
	$(PYTHON_LIBS)

noinst_HEADERS = include/kopano/conversion.h include/kopano/director_util.h \
	include/kopano/typemap_python.i

BUILT_SOURCES = mapi_wrap.cxx \
				mapi_wrap.h \
				inetmapi_wrap.cxx \
				icalmapi_wrap.cxx \
				archiver_wrap.cxx \
				libfreebusy_wrap.cxx \
				RecurrenceState_wrap.cxx


CLEANFILES += ${BUILT_SOURCES} \
			 MAPICore.py \
			 inetmapi.py \
			 icalmapi.py \
			 archiver.py \
			 libfreebusy.py

python.sym: ${top_builddir}/config.status

# wait for full output of swig
MAPICore.py: $(BUILT_SOURCES)
inetmapi.py: $(BUILT_SOURCES)
icalmapi.py: $(BUILT_SOURCES)
archiver.py: $(BUILT_SOURCES)
libfreebusy.py: $(BUILT_SOURCES)
RecurrenceState.py: $(BUILT_SOURCES)

nodist__MAPICore_la_SOURCES = mapi_wrap.cxx mapi_wrap.h
nodist__inetmapi_la_SOURCES = inetmapi_wrap.cxx
nodist__icalmapi_la_SOURCES = icalmapi_wrap.cxx
nodist__archiver_la_SOURCES = archiver_wrap.cxx
_archiver_la_SOURCES = archiver_conv.h archiver_conv.cpp
nodist__RecurrenceState_la_SOURCES = RecurrenceState_wrap.cxx
nodist__libfreebusy_la_SOURCES = libfreebusy_wrap.cxx
_libfreebusy_la_SOURCES = libfreebusy_conv.h libfreebusy_conv.cpp

EXTRA_DIST = helpers_python.i \
	setup.py \
	setup.cfg \
	MAPICore.py \
	inetmapi.py \
	icalmapi.py \
	libfreebusy.py \
	MAPI/Defs.py \
	MAPI/__init__.py \
	MAPI/version.py \
	MAPI/Struct.py \
	MAPI/Tags.py \
	MAPI/Time.py \
	MAPI/Util/__init__.py \
	MAPI/Util/AddressBook.py \
	MAPI/Util/codepage.py \
	archiver_python.i

datadir = ${pythondir}
data_DATA = MAPICore.py inetmapi.py icalmapi.py libfreebusy.py
noinst_datadir = RecurrenceState.py

DEPFLAGS = -MMD -MF .$@.dep

mapi_wrap.h: mapi_wrap.cxx
mapi_wrap.cxx: ../mapi.i
	if test "x$(SWIG_EXEC)" = "x"; then echo "swig is required for 'make dist'"; exit 1; fi
	${SWIG_EXEC} -I${top_srcdir}/common/include ${DEPFLAGS} ${SWIG_FLAGS} ${SWIG_CXXFLAGS} -o $@ $<

inetmapi_wrap.cxx: ../inetmapi.i
	${SWIG_EXEC} ${DEPFLAGS} ${SWIG_CXXFLAGS} -o $@ $<

icalmapi_wrap.cxx: ../icalmapi.i
	${SWIG_EXEC} -I${top_srcdir}/libicalmapi -I${top_srcdir}/libfreebusy ${DEPFLAGS} ${SWIG_CXXFLAGS} -o $@ $<

archiver_wrap.cxx: ../archiver.i
	${SWIG_EXEC} -I${top_srcdir}/ECtools/archiver ${DEPFLAGS} ${SWIG_CXXFLAGS} -o $@ $<

RecurrenceState_wrap.cxx: ../RecurrenceState.i
	${SWIG_EXEC} -I${top_srcdir}/common -I${top_srcdir}/m4lcommon ${DEPFLAGS} ${SWIG_CXXFLAGS} -o $@ $<

libfreebusy_wrap.cxx: ../libfreebusy.i
	${SWIG_EXEC} -I${top_srcdir}/libfreebusy ${DEPFLAGS} ${SWIG_CXXFLAGS} -o $@ $<

-include .*.dep

check-syntax:
	$(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
		-fsyntax-only -fmessage-length=0 ${CHK_SOURCES} -Wall -Wformat=2

install-exec-local:
	rm -Rf staging
	${MKDIR_P} staging
	cp -a ${srcdir}/setup.py ${srcdir}/setup.cfg ${srcdir}/MAPI staging/
	cp -a MAPI/version.py staging/MAPI/
	cd staging/ && ${PYTHON} setup.py \
		build --build-base="${abs_builddir}/build" \
		install -f $${DESTDIR+--root=${DESTDIR}} \
		--prefix="${prefix}" --install-purelib="${pythondir}"
	rm -Rf build

uninstall-local:
	rm -Rf ${DESTDIR}${pythondir}/MAPI \
		${DESTDIR}${pythondir}/MAPI-*.egg-info

PYTEST = pytest

.PHONY: test
test:
	MAPI_CONFIG_PATH=../../provider/client PYTHONPATH=.libs:. ${PYTEST} --junitxml=test.xml tests

include ../../global.am
