# -*- Makefile -*-

systemunit_DATA = kopano-dagent.service kopano-gateway.service \
                  kopano-ical.service kopano-monitor.service \
                  kopano-presence.service kopano-search.service \
                  kopano-server.service kopano-spooler.service
sysusers_DATA = kopano-sysusers.conf
tmpfiles_DATA = kopano-tmpfiles.conf

config_files = \
	server.cfg unix.cfg ldap.propmap.cfg presence.cfg \
	spooler.cfg gateway.cfg monitor.cfg dagent.cfg ical.cfg search.cfg \
	autorespond archiver.cfg backup.cfg

rhel_initscript_files = \
	kopano-server.init.rhel kopano-spooler.init.rhel \
	kopano-gateway.init.rhel kopano-monitor.init.rhel \
	kopano-ical.init.rhel kopano-dagent.init.rhel kopano-search.init.rhel \
	kopano-presence.init.rhel
suse_initscript_files = \
	kopano-server.init.suse kopano-spooler.init.suse \
	kopano-gateway.init.suse kopano-monitor.init.suse \
	kopano-ical.init.suse kopano-dagent.init.suse kopano-search.init.suse \
	kopano-presence.init.suse
sles_initscript_files = $(suse_initscript_files)
cbs4_initscript_files = \
	kopano-server.init.cbs4 kopano-spooler.init.cbs4 \
	kopano-gateway.init.cbs4 kopano-monitor.init.cbs4 \
	kopano-ical.init.cbs4 kopano-dagent.init.cbs4 kopano-search.init.cbs4 \
	kopano-presence.init.cbs4
alt_initscript_files = \
	kopano-server.init.alt kopano-spooler.init.alt \
	kopano-gateway.init.alt kopano-monitor.init.alt \
	kopano-ical.init.alt kopano-dagent.init.alt kopano-search.init.alt \
	kopano-presence.init.alt
debian_initscript_files = \
	kopano-server.init.debi kopano-spooler.init.debi \
	kopano-gateway.init.debi kopano-monitor.init.debi \
	kopano-ical.init.debi kopano-dagent.init.debi kopano-search.init.debi \
	kopano-presence.init.debi

data_configdir = ${docdir}/example-config
dist_data_config_DATA = ${config_files}

sysconf_configdir = ${sysconfdir}/kopano
dist_sysconf_config_DATA = ${config_files}

sysconf_initdir=${sysconfdir}/init.d
sysconf_init_SCRIPTS = ${@DISTRO@_initscript_files}

rhel_sysconfig_path = ${sysconfdir}/sysconfig
suse_sysconfig_path = ${localstatedir}/adm/fillup-templates
sles_sysconfig_path = ${localstatedir}/adm/fillup-templates
sysconf_sysconfigdir = ${@DISTRO@_sysconfig_path}
sysconf_sysconfig_DATA = sysconfig.txt
alt_sysconfig_path = ${sysconfdir}/sysconfig

dist_doc_DATA = \
	db-convert-4.1-to-4.2 db-convert-4.20-to-4.21 db-convert-4.20-to-innodb.sql db-calc-storesize db-convert-attachments-to-files \
	createuser.dotforward db-convert-4.2x-to-5.00 ssl-certificates.sh db-upgrade-objectsid-to-objectguid.pl ldap-switch-sendas.pl \
	audit-parse.pl kopano7-upgrade db-remove-orphaned-attachments \
	${top_srcdir}/tools/python-scripts/optimize-imap.py ${top_srcdir}/tools/python-scripts/update-resource-recipients

dist_pkgdata_DATA = ldap.m4

BUILT_SOURCES = ldap.openldap.cfg ldap.active-directory.cfg

ldap.openldap.cfg: ldap.m4
	m4 -DTYPE=OPENLDAP ${top_srcdir}/installer/linux/ldap.m4 > $@

ldap.active-directory.cfg: ldap.m4
	m4 -DTYPE=ADS ${top_srcdir}/installer/linux/ldap.m4 > $@

ldapms.openldap.cfg: ldap.m4
	m4 -DTYPE=OPENLDAP -DMULTISERVER=1 ${top_srcdir}/installer/linux/ldap.m4 > $@

ldapms.active-directory.cfg: ldap.m4
	m4 -DTYPE=ADS -DMULTISERVER=1 ${top_srcdir}/installer/linux/ldap.m4 > $@

dist_sbin_SCRIPTS = kopano-autorespond kopano-mr-accept kopano-mr-process

sysconf_rotatedir = ${sysconfdir}/logrotate.d
dist_sysconf_rotate_DATA = kopano.logrotate

sysconf_crondir = ${sysconfdir}/cron.daily
dist_sysconf_cron_SCRIPTS = kopano-client-update.cron

sysconf_bcdir = ${sysconfdir}/bash_completion.d
dist_sysconf_bc_DATA = kopano-bash-completion.sh

sysconf_certdir = ${sysconfdir}/kopano/report-ca

config_files += $(BUILT_SOURCES)
EXTRA_DIST = $(BUILT_SOURCES) \
	$(rhel_initscript_files) $(suse_initscript_files) $(cbs4_initscript_files) $(alt_initscript_files) \
	sysconfig.txt

install-data-hook:
	install -d -m 755 ${DESTDIR}/${sysconf_configdir}/license
	install -d -m 755 ${DESTDIR}/${localstatedir}/lib/kopano/report
	mv -v ${DESTDIR}${sysconf_rotatedir}/kopano.logrotate ${DESTDIR}${sysconf_rotatedir}/kopano
	mv -v ${DESTDIR}${sysconf_crondir}/kopano-client-update.cron ${DESTDIR}${sysconf_crondir}/kopano-client-update
	if test ${DISTRO} = "sles" -o ${DISTRO} = "suse"; then \
		mv -v ${DESTDIR}${sysconf_sysconfigdir}/sysconfig.txt ${DESTDIR}${sysconf_sysconfigdir}/sysconfig.kopano; \
	elif test ${DISTRO} = "rhel" -o ${DISTRO} = "alt"; then \
		mv -v ${DESTDIR}${sysconf_sysconfigdir}/sysconfig.txt ${DESTDIR}${sysconf_sysconfigdir}/kopano; \
	fi
	for f in ${sysconf_init_SCRIPTS}; do \
		newf=`echo "$$f" | sed -e 's/\(kopano-.*\).init.*/\1/'`; \
		mv -v "${DESTDIR}${sysconf_initdir}/$$f" "${DESTDIR}${sysconf_initdir}/$$newf"; \
	done
	mkdir -p ${DESTDIR}/${docdir}/../kopano-gateway
	mv -v ${DESTDIR}/${docdir}/optimize-imap.py ${DESTDIR}/${docdir}/../kopano-gateway
	rm -f ${DESTDIR}/${sysconf_configdir}/example-msr.cfg
