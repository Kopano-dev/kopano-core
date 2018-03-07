# -*- Makefile -*-

EXTRA_DIST = setup.py \
	kopano_rest/__init__.py

install-exec-local:
	rm -Rf staging
	${MKDIR_P} staging
	cp -a ${srcdir}/setup.py ${srcdir}/kopano_rest staging/
	cp -a kopano_rest/version.py staging/kopano_rest/
	cd staging/ && ${PYTHON} setup.py \
		build --build-base="${abs_builddir}/build" \
		install -f $${DESTDIR+--root=${DESTDIR}} \
		--prefix ${prefix} --install-purelib ${pythondir}
	rm -Rf build

uninstall-local:
	rm -Rf ${DESTDIR}${pythondir}/kopano_rest \
		${DESTDIR}${pythondir}/kopano_rest-*.egg-info
