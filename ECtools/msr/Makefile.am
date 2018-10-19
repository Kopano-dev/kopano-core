dist_sbin_SCRIPTS = kopano-msr

EXTRA_DIST = setup.py		\
	setup.cfg \
	kopano_msr/__init__.py

install-exec-local:
	rm -Rf staging
	${MKDIR_P} staging
	cp -a ${srcdir}/setup.py ${srcdir}/setup.cfg ${srcdir}/kopano_msr staging/
	cp -a kopano_msr/version.py staging/kopano_msr/
	cd staging/ && ${PYTHON} setup.py \
		build --build-base="${abs_builddir}/build" \
		install -f $${DESTDIR+--root=${DESTDIR}} \
		--prefix="${prefix}" --install-purelib ${pythondir}
	rm -Rf build

install-data-local:
	mkdir -p "${DESTDIR}${localstatedir}/lib/kopano/msr"

uninstall-local:
	rm -Rf ${DESTDIR}${pythondir}/kopano_msr \
		${DESTDIR}${pythondir}/kopano_msr-*.egg-info
