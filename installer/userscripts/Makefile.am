xdirs = createcompany.d creategroup.d createuser.d deletecompany.d deletegroup.d deleteuser.d
EXTRA_DIST = createcompany.in creategroup.in createuser.in deletecompany.in deletegroup.in deleteuser.in
dist_bin_SCRIPTS = createcompany creategroup createuser deletecompany deletegroup deleteuser
bindir = ${dcprefix}${uscriptlibdir}
cudir = ${bindir}/createuser.d
dist_cu_SCRIPTS = 00createstore
ccdir = ${bindir}/createcompany.d
dist_cc_SCRIPTS = 00createpublic

replcmd = ${AM_V_GEN}${SED} -e 's|[@]pkglibexecdir@|${pkglibexecdir}|g' -e 's|[@]uscriptlibdir@|${uscriptlibdir}|g' -e 's|[@]uscriptconfdir@|${uscriptconfdir}|g' <"$<" >"$@"

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

install-data-hook:
	for dir in ${xdirs}; do \
		mkdir -p ${DESTDIR}/${dcprefix}${uscriptlibdir}/$$dir ${DESTDIR}/${dcprefix}${uscriptconfdir}/$$dir; \
	done
