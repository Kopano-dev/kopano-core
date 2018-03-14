dist_sbin_SCRIPTS = kopano-search kopano-search-xapian-compact.py kopano-search-upgrade-findroots.py

EXTRA_DIST = setup.py		\
	setup.cfg \
	kopano_search/__init__.py \
	kopano_search/plaintext.py \
	kopano_search/magic.py \
	kopano_search/plugin_solr.py  \
	kopano_search/plugin_xapian.py  \
	kopano_search/xmltotext.xslt

install-exec-local:
	rm -Rf staging
	${MKDIR_P} staging
	cp -a ${srcdir}/setup.py ${srcdir}/setup.cfg ${srcdir}/kopano_search staging/
	cp -a kopano_search/version.py staging/kopano_search/
	cd staging/ && ${PYTHON} setup.py \
		build --build-base="${abs_builddir}/build" \
		install -f $${DESTDIR+--root=${DESTDIR}} \
		--prefix="${prefix}" --install-purelib ${pythondir}
	rm -Rf build

install-data-local:
	mkdir -p "${DESTDIR}${localstatedir}/lib/kopano/search"

uninstall-local:
	rm -Rf ${DESTDIR}${pythondir}/kopano_search \
		${DESTDIR}${pythondir}/kopano_search-*.egg-info
