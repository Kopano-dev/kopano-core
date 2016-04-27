dist_sbin_SCRIPTS = kopano-search kopano-search-xapian-compact.py

EXTRA_DIST = setup.py		\
	kopano_search/__init__.py \
	kopano_search/plaintext.py \
	kopano_search/magic.py \
	kopano_search/plugin_solr.py  \
	kopano_search/plugin_xapian.py  \
	kopano_search/xmltotext.xslt

install-exec-local:
	cd ${srcdir} && ${PYTHON} setup.py install -f \
		$${DESTDIR+--root=${DESTDIR}} \
		--prefix="${prefix}" --install-purelib ${pythondir}
	rm -Rf build

install-data-local:
	mkdir -p "${DESTDIR}${localstatedir}/lib/kopano/search"
