# -*- Makefile -*-

dist_sbin_SCRIPTS = kopano-autorespond.py kopano-mr-accept.py kopano-mr-process.py 

EXTRA_DIST = setup.py \
	setup.cfg \
	kopano_utils/__init__.py \
	kopano_utils/autorespond.py \
	kopano_utils/mr_accept.py \
	kopano_utils/mr_process.py

install-exec-local:
	rm -Rf staging
	${MKDIR_P} staging
	cp -a ${srcdir}/setup.py ${srcdir}/kopano_utils staging/
	cp -a kopano_utils/version.py staging/kopano_utils/
	cd staging/ && ${PYTHON} setup.py \
		build --build-base="${abs_builddir}/build" \
		install -f $${DESTDIR+--root=${DESTDIR}} \
		--prefix ${prefix} --install-purelib ${pythondir}
	rm -Rf build staging

uninstall-local:
	rm -Rf ${DESTDIR}${pythondir}/kopano_utils \
		${DESTDIR}${pythondir}/kopano_utils-*.egg-info
