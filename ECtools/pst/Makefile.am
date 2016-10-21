# -*- Makefile -*-

dist_sbin_SCRIPTS = kopano-pst

EXTRA_DIST = setup.py \
	kopano_pst/__init__.py

install-exec-local:
	cd ${srcdir} && ${PYTHON} setup.py \
		build --build-base="${abs_builddir}/build" \
		install -f $${DESTDIR+--root=${DESTDIR}} \
		--prefix ${prefix} --install-purelib ${pythondir}
	rm -Rf build

uninstall-local:
	rm -Rf ${DESTDIR}${pythondir}/kopano_pst \
		${DESTDIR}${pythondir}/kopano_pst-0*.egg-info
