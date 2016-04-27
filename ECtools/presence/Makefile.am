# -*- Makefile -*-

dist_sbin_SCRIPTS = kopano-presence

EXTRA_DIST = setup.py kopano_presence/__init__.py \
             kopano_presence/plugin_xmpp.py kopano_presence/plugin_spreed.py

install-exec-local:
	cd ${srcdir} && ${PYTHON} setup.py install -f \
		$${DESTDIR+--root=${DESTDIR}} \
		--prefix="${prefix}" --install-purelib ${pythondir}
	rm -Rf build
