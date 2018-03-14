# -*- Makefile -*-

dist_sbin_SCRIPTS = kopano-presence

EXTRA_DIST = setup.py \
	setup.cfg \
	kopano_presence/__init__.py \
	kopano_presence/version.py \
	kopano_presence/plugin_xmpp.py \
	kopano_presence/plugin_spreed.py

install-exec-local:
	rm -Rf staging
	${MKDIR_P} staging
	cp -a ${srcdir}/setup.py ${srcdir}/setup.cfg ${srcdir}/kopano_presence staging/
	cp -a kopano_presence/version.py staging/kopano_presence/
	cd staging/ && ${PYTHON} setup.py \
		build --build-base="${abs_builddir}/build" \
		install -f $${DESTDIR+--root=${DESTDIR}} \
		--prefix="${prefix}" --install-purelib ${pythondir}
	rm -Rf build

uninstall-local:
	rm -Rf ${DESTDIR}${pythondir}/kopano_presence \
		${DESTDIR}${pythondir}/kopano_presence-*.egg-info
