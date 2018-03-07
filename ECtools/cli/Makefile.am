# -*- Makefile -*-

dist_sbin_SCRIPTS = kopano-cli

EXTRA_DIST = setup.py \
	kopano_cli/__init__.py

install-exec-local:
	rm -Rf staging
	${MKDIR_P} staging
	cp -a ${srcdir}/setup.py ${srcdir}/kopano_cli staging/
	cp -a kopano_cli/version.py staging/kopano_cli/
	cd staging/ && ${PYTHON} setup.py \
		build --build-base="${abs_builddir}/build" \
		install -f $${DESTDIR+--root=${DESTDIR}} \
		--prefix ${prefix} --install-purelib ${pythondir}
	rm -Rf build

uninstall-local:
	rm -Rf ${DESTDIR}${pythondir}/kopano_cli \
		${DESTDIR}${pythondir}/kopano_cli-*.egg-info
