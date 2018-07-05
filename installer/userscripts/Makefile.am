xdirs = createcompany.d creategroup.d createuser.d deletecompany.d deletegroup.d deleteuser.d
dist_bin_SCRIPTS = createcompany creategroup createuser deletecompany deletegroup deleteuser
bindir = ${dcprefix}${uscriptlibdir}
cudir = ${bindir}/createuser.d
cu_SCRIPTS = 00createstore
ccdir = ${bindir}/createcompany.d
cc_SCRIPTS = 00createpublic

replcmd = ${AM_V_GEN}${SED} -e 's|[@]uscriptlibdir@|${uscriptlibdir}|g' -e 's|[@]uscriptconfdir@|${uscriptconfdir}|g' <"$<" >"$@"

createcompany: createcompany.in
	${replcmd}
deletecompany: deletecompany.in
	${replcmd}
creategroup: creategroup.in
	${replcmd}
deletegroup: deletegroup.in
	${replcmd}
createuser: createuser.in
	${replcmd}
deleteuser: deleteuser.in
	${replcmd}

install-data-hook:
	for dir in ${xdirs}; do \
		mkdir -p ${DESTDIR}/${dcprefix}${uscriptlibdir}/$$dir ${DESTDIR}/${dcprefix}${uscriptconfdir}/$$dir; \
	done
