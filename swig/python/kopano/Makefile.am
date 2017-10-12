EXTRA_DIST = setup.py		\
	kopano/__init__.py \
	kopano/version.py \
	kopano/lockfile.py \
	kopano/daemon/LICENSE \
	kopano/daemon/__init__.py \
	kopano/daemon/daemon.py \
	kopano/daemon/pidlockfile.py \
	kopano/daemon/runner.py \
	kopano/daemon/version/__init__.py \
	kopano/daemon/version/version_info.py

install-exec-local:
	cd ${srcdir} && ${PYTHON} setup.py \
		build --build-base="${abs_builddir}/build" \
		install -f $${DESTDIR+--root=${DESTDIR}} \
		--prefix="${prefix}" --install-purelib ${pythondir}
	rm -Rf build

uninstall-local:
	rm -Rf ${DESTDIR}${pythondir}/kopano \
		${DESTDIR}${pythondir}/kopano-0*.egg-info
