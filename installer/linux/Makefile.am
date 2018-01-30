# -*- Makefile -*-

systemunitdir = ${dcprefix}@systemunitdir@
sysusersdir = ${dcprefix}@sysusersdir@
tmpfilesdir = ${dcprefix}@tmpfilesdir@

dist_systemunit_DATA = kopano-dagent.service kopano-gateway.service \
                  kopano-ical.service kopano-monitor.service \
                  kopano-presence.service kopano-search.service \
                  kopano-server.service kopano-spooler.service \
                  kopano-spamd.service

dist_sysusers_DATA = kopano-sysusers.conf
dist_tmpfiles_DATA = kopano-tmpfiles.conf
dist_pkgdata_DATA = ldap.active-directory.cfg ldap.openldap.cfg ldap.propmap.cfg

config_files = \
	server.cfg unix.cfg presence.cfg \
	spooler.cfg gateway.cfg monitor.cfg dagent.cfg ical.cfg search.cfg \
	autorespond archiver.cfg backup.cfg migration-pst.cfg \
	autorespond.cfg

rhel_initscript_files = \
	kopano-server.init.rhel kopano-spooler.init.rhel \
	kopano-gateway.init.rhel kopano-monitor.init.rhel \
	kopano-ical.init.rhel kopano-dagent.init.rhel kopano-search.init.rhel \
	kopano-presence.init.rhel kopano-spamd.init.rhel

data_configdir = ${docdir}/example-config
dist_data_config_DATA = ${config_files} ldap.cfg

sysconf_initdir=${sysconfdir}/init.d
sysconf_init_SCRIPTS = ${@DISTRO@_initscript_files}

rhel_sysconfig_path = ${sysconfdir}/sysconfig
suse_sysconfig_path = ${localstatedir}/adm/fillup-templates
sles_sysconfig_path = ${localstatedir}/adm/fillup-templates
sysconf_sysconfigdir = ${@DISTRO@_sysconfig_path}
sysconf_sysconfig_DATA = sysconfig.txt

dist_doc_DATA = \
	db-calc-storesize db-convert-attachments-to-files \
	createuser.dotforward ssl-certificates.sh ldap-switch-sendas.pl \
	audit-parse.pl db-remove-orphaned-attachments \
	${top_srcdir}/tools/python-scripts/update-resource-recipients
kgwdocdir = ${docdir}/../kopano-gateway
dist_kgwdoc_DATA = ${top_srcdir}/tools/python-scripts/optimize-imap.py

sysconf_apparmordir = ${docdir}/example-config/apparmor.d
dist_sysconf_apparmor_DATA = \
	usr.sbin.kopano-dagent \
	usr.sbin.kopano-search \
	usr.sbin.kopano-server

BUILT_SOURCES = ldap.openldap.cfg ldap.active-directory.cfg

ldap.openldap.cfg: ldap.m4
	m4 -DTYPE=OPENLDAP ${top_srcdir}/installer/linux/ldap.m4 > $@

ldap.active-directory.cfg: ldap.m4
	m4 -DTYPE=ADS ${top_srcdir}/installer/linux/ldap.m4 > $@

dist_sbin_SCRIPTS = kopano-autorespond kopano-mr-accept kopano-mr-process

sysconf_rotatedir = ${sysconfdir}/logrotate.d
dist_sysconf_rotate_DATA = kopano.logrotate

sysconf_bcdir = ${sysconfdir}/bash_completion.d
dist_sysconf_bc_DATA = kopano-bash-completion.sh

EXTRA_DIST = ${BUILT_SOURCES} ldap.m4 \
	$(rhel_initscript_files) sysconfig.txt

install-data-hook:
	install -d -m 755 ${DESTDIR}/${sysconfdir}/kopano/license
	mv -v ${DESTDIR}${sysconf_rotatedir}/kopano.logrotate ${DESTDIR}${sysconf_rotatedir}/kopano
	if test ${DISTRO} = "sles" -o ${DISTRO} = "suse"; then \
		mv -v ${DESTDIR}${sysconf_sysconfigdir}/sysconfig.txt ${DESTDIR}${sysconf_sysconfigdir}/sysconfig.kopano; \
	elif test ${DISTRO} = "rhel"; then \
		mv -v ${DESTDIR}${sysconf_sysconfigdir}/sysconfig.txt ${DESTDIR}${sysconf_sysconfigdir}/kopano; \
	fi
	for f in ${sysconf_init_SCRIPTS}; do \
		newf=`echo "$$f" | sed -e 's/\(kopano-.*\).init.*/\1/'`; \
		mv -v "${DESTDIR}${sysconf_initdir}/$$f" "${DESTDIR}${sysconf_initdir}/$$newf"; \
	done

uninstall-local:
	rm -f "${DESTDIR}${sysconf_rotatedir}/kopano"
	if test ${DISTRO} = "sles" -o ${DISTRO} = "suse"; then \
		rm -f "${DESTDIR}${sysconf_sysconfigdir}/sysconfig.kopano"; \
	elif test ${DISTRO} = "rhel"; then \
		rm -f "${DESTDIR}${sysconf_sysconfigdir}/kopano"; \
	fi
	for f in ${sysconf_init_SCRIPTS}; do \
		newf=$$(echo "$$f" | sed -e 's/\(kopano-.*\).init.*/\1/'); \
		rm -f "${DESTDIR}${sysconf_initdir}/$$newf"; \
	done
