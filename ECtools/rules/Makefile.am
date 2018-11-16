# -*- Makefile -*-

dist_sbin_SCRIPTS = kopano-rules

EXTRA_DIST = setup.py \
	setup.cfg \
	kopano_rules/__init__.py

install-exec-local:
	rm -Rf staging
	${MKDIR_P} staging
	cp -a ${srcdir}/setup.py ${srcdir}/setup.cfg ${srcdir}/kopano_rules staging/
	cp -a kopano_rules/version.py staging/kopano_rules/
	cd staging/ && ${PYTHON} setup.py \
		build --build-base="${abs_builddir}/build" \
		install -f $${DESTDIR+--root=${DESTDIR}} \
		--prefix ${prefix} --install-purelib ${pythondir}
	rm -Rf build

uninstall-local:
	rm -Rf ${DESTDIR}${pythondir}/kopano_rules \
		${DESTDIR}${pythondir}/kopano_rules-*.egg-info
