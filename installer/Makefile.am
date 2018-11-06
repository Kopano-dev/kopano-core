SUBDIRS = linux userscripts quotamail searchscripts

datadir = ${docdir}
dist_data_DATA = ldap/kopano.schema kopano.ldif
BUILT_SOURCES = kopano.ldif
EXTRA_DIST = ldap/ol-schema-migrate.pl

$(BUILT_SOURCES): ldap/kopano.schema ldap/ol-schema-migrate.pl
	${top_srcdir}/installer/ldap/ol-schema-migrate.pl -X -b ${top_srcdir}/installer/ldap/kopano.schema >${top_builddir}/installer/kopano.ldif
