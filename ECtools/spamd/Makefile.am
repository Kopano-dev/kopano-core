# -*- Makefile -*-

dist_sbin_SCRIPTS = kopano-spamd

EXTRA_DIST = setup.py \
	setup.cfg \
	kopano_spamd/__init__.py

install-exec-local:
	rm -Rf staging
	${MKDIR_P} staging
	cp -a ${srcdir}/setup.py ${srcdir}/kopano_spamd staging/
	cp -a kopano_spamd/version.py staging/kopano_spamd/
	cd staging/ && ${PYTHON} setup.py \
		build --build-base="${abs_builddir}/build" \
		install -f $${DESTDIR+--root=${DESTDIR}} \
		--prefix ${prefix} --install-purelib ${pythondir}
	rm -Rf build staging

uninstall-local:
	rm -Rf ${DESTDIR}${pythondir}/kopano_spamd \
		${DESTDIR}${pythondir}/kopano_spamd-*.egg-info
