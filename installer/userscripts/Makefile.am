SUBDIRS = createcompany.d creategroup.d createuser.d deletecompany.d deletegroup.d deleteuser.d
dist_bin_SCRIPTS = createcompany creategroup createuser deletecompany deletegroup deleteuser
bindir = ${dcprefix}${USERSCRIPTDIR}
dist_data_DATA = users_common.sh groups_common.sh companies_common.sh
datadir = ${dcprefix}${USERSCRIPTDIR}

install-data-hook:
	for dir in $(SUBDIRS); do \
		mkdir -p ${DESTDIR}/${dcprefix}${USERSCRIPTDIR}/$$dir; \
	done
