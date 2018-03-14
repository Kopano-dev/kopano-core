# -*- Makefile -*-

dist_sbin_SCRIPTS = kopano-migration-pst

EXTRA_DIST = setup.py \
	setup.cfg \
	kopano_migration_pst/__init__.py \
	kopano_migration_pst/version.py

install-exec-local:
	rm -Rf staging
	${MKDIR_P} staging
	cp -a ${srcdir}/setup.py ${srcdir}/setup.cfg ${srcdir}/kopano_migration_pst staging/
	cp -a kopano_migration_pst/version.py staging/kopano_migration_pst/
	cd staging/ && ${PYTHON} setup.py \
		build --build-base="${abs_builddir}/build" \
		install -f $${DESTDIR+--root=${DESTDIR}} \
		--prefix ${prefix} --install-purelib ${pythondir}
	rm -Rf build

uninstall-local:
	rm -Rf ${DESTDIR}${pythondir}/kopano_migration_pst \
		${DESTDIR}${pythondir}/kopano_migration_pst-*.egg-info
