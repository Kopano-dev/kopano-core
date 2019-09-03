# -*- Makefile -*-

dist_sbin_SCRIPTS = kopano-backup

EXTRA_DIST = requirements.txt setup.py \
	setup.cfg \
	kopano_backup/__init__.py

install-exec-local:
	rm -Rf staging
	${MKDIR_P} staging
	cp -a ${srcdir}/setup.py ${srcdir}/setup.cfg ${srcdir}/kopano_backup staging/
	cp -a kopano_backup/version.py staging/kopano_backup/
	cd staging/ && ${PYTHON} setup.py \
		build --build-base="${abs_builddir}/build" \
		install -f $${DESTDIR+--root=${DESTDIR}} \
		--prefix="${prefix}" --install-purelib="${pythondir}"
	rm -Rf build staging

uninstall-local:
	rm -Rf ${DESTDIR}${pythondir}/kopano_backup \
		${DESTDIR}${pythondir}/kopano_backup-*.egg-info
