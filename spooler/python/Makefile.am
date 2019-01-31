SUBDIRS=plugins

EXTRA_DIST = mapiplugin.py pluginmanager.py plugintemplates.py wraplogger.py wraplogger.py zconfig.py zinterval.py

kopano_dagentdir = ${datadir}/kopano-dagent/python
kopano_dagent_DATA = mapiplugin.py pluginmanager.py plugintemplates.py wraplogger.py zconfig.py zinterval.py

kopano_spoolerdir = ${datadir}/kopano-spooler/python
kopano_spooler_DATA = mapiplugin.py pluginmanager.py plugintemplates.py wraplogger.py zconfig.py zinterval.py

install-data-hook:
	mkdir -p ${DESTDIR}/${localstatedir}/lib/kopano/dagent/plugins/
	mkdir -p ${DESTDIR}/${localstatedir}/lib/kopano/spooler/plugins/
