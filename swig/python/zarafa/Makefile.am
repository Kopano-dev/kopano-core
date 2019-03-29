EXTRA_DIST = setup.py zarafa/__init__.py

install-exec-local:
	cd ${srcdir} && ${PYTHON} setup.py \
		build --build-base="${abs_builddir}/build" \
		install -f $${DESTDIR+--root=${DESTDIR}} \
		--prefix="${prefix}" --install-purelib="${pythondir}"
	rm -Rf build

uninstall-local:
	rm -Rf ${DESTDIR}${pythondir}/zarafa \
		${DESTDIR}${pythondir}/zarafa-0*.egg-info
