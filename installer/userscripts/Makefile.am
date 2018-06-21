SUBDIRS = createcompany.d creategroup.d createuser.d deletecompany.d deletegroup.d deleteuser.d
dist_bin_SCRIPTS = createcompany creategroup createuser deletecompany deletegroup deleteuser
bindir = ${dcprefix}${uscriptlibdir}

replcmd = ${SED} -e 's|[@]uscriptlibdir@|${uscriptlibdir}|g' -e 's|[@]uscriptconfdir@|${uscriptconfdir}|g' <"$<" >"$@"

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
	for dir in $(SUBDIRS); do \
		mkdir -p ${DESTDIR}/${dcprefix}${uscriptlibdir}/$$dir ${DESTDIR}/${dcprefix}${uscriptconfdir}/$$dir; \
	done
