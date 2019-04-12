EXTRA_DIST = setup.cfg setup.py \
	kopano/__init__.py

install-exec-local:
	rm -Rf staging
	${MKDIR_P} staging
	cp -a ${srcdir}/setup.py ${srcdir}/setup.cfg ${srcdir}/kopano staging/
	cp -a kopano/version.py staging/kopano/
	cd staging/ && ${PYTHON} setup.py \
		build --build-base="${abs_builddir}/build" \
		install -f $${DESTDIR+--root=${DESTDIR}} \
		--prefix="${prefix}" --install-purelib="${pythondir}"
	rm -Rf build staging

uninstall-local:
	rm -Rf ${DESTDIR}${pythondir}/kopano \
		${DESTDIR}${pythondir}/kopano-*.egg-info
