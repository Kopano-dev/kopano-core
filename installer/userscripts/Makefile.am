SUBDIRS = createcompany.d creategroup.d createuser.d deletecompany.d deletegroup.d deleteuser.d
dist_bin_SCRIPTS = createcompany creategroup createuser deletecompany deletegroup deleteuser
bindir = ${dcprefix}${USERSCRIPTDIR}
dist_data_DATA = users_common.sh groups_common.sh companies_common.sh
datadir = ${dcprefix}${USERSCRIPTDIR}
replcmd = ${AM_V_GEN}${SED} -e 's|[@]sysconfdir@|${sysconfdir}|g' -e 's|[@]PKGLIBEXECDIR@|${pkglibexecdir}|g' -e 's|[@]USERSCRIPTDIR@|${USERSCRIPTDIR}|g' <"$<" >"$@"

install-data-hook:
	for dir in $(SUBDIRS); do \
		mkdir -p ${DESTDIR}/${dcprefix}${USERSCRIPTDIR}/$$dir; \
	done

createcompany: createcompany.in ${top_builddir}/config.status
	${replcmd}
deletecompany: deletecompany.in ${top_builddir}/config.status
	${replcmd}
creategroup: creategroup.in ${top_builddir}/config.status
	${replcmd}
deletegroup: deletegroup.in ${top_builddir}/config.status
	${replcmd}
createuser: createuser.in ${top_builddir}/config.status
	${replcmd}
deleteuser: deleteuser.in ${top_builddir}/config.status
	${replcmd}
