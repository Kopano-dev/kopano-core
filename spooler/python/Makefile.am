MAPI_CONFIG_PATH = ${top_srcdir}/provider/client:${top_srcdir}/provider/contacts
PYTHONPATH = ${top_srcdir}/swig/python/.libs:${top_srcdir}/swig/python/:.
PLUGIN_PATH ?= ${top_srcdir}/spooler/python/plugins
PYTEST ?= pytest

EXTRA_DIST = mapiplugin.py pluginmanager.py plugintemplates.py wraplogger.py wraplogger.py zconfig.py

kopano_dagentdir = ${datadir}/kopano-dagent/python
kopano_dagent_DATA = mapiplugin.py pluginmanager.py plugintemplates.py wraplogger.py zconfig.py

kopano_spoolerdir = ${datadir}/kopano-spooler/python
kopano_spooler_DATA = mapiplugin.py pluginmanager.py plugintemplates.py wraplogger.py zconfig.py

install-data-hook:
	mkdir -p ${DESTDIR}/${localstatedir}/lib/kopano/dagent/plugins/
	mkdir -p ${DESTDIR}/${localstatedir}/lib/kopano/spooler/plugins/

.PHONY: test
test:
	MAPI_CONFIG_PATH=${MAPI_CONFIG_PATH} PYTHONPATH=${PYTHONPATH} PLUGIN_PATH=${PLUGIN_PATH} \
		${PYTEST} ${PYTEST_OPTIONS} --junitxml=test.xml ${top_srcdir}/spooler/python/tests
