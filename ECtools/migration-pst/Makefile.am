# -*- Makefile -*-

dist_sbin_SCRIPTS = kopano-migration-pst

EXTRA_DIST = setup.py \
	kopano_migration_pst/__init__.py kopano_migration_pst/version.py

install-exec-local:
	cd ${srcdir} && ${PYTHON} setup.py \
		build --build-base="${abs_builddir}/build" \
		install -f $${DESTDIR+--root=${DESTDIR}} \
		--prefix ${prefix} --install-purelib ${pythondir}
	rm -Rf build

uninstall-local:
	rm -Rf ${DESTDIR}${pythondir}/kopano_migration_pst \
		${DESTDIR}${pythondir}/kopano_migration_pst-0*.egg-info
