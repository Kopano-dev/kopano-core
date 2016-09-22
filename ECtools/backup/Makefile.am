# -*- Makefile -*-

dist_sbin_SCRIPTS = kopano-backup

EXTRA_DIST = setup.py \
	kopano_backup/__init__.py

install-exec-local:
	cd ${srcdir} && ${PYTHON} setup.py \
		build --build-base="${abs_builddir}/build" \
		install -f $${DESTDIR+--root=${DESTDIR}} \
		--prefix ${prefix} --install-purelib ${pythondir}
	rm -Rf build

uninstall-local:
	rm -Rf ${DESTDIR}${pythondir}/kopano_backup \
		${DESTDIR}${pythondir}/kopano_backup-0*.egg-info
