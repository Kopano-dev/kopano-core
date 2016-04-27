# -*- Makefile -*-

dist_man_MANS = ${genpages}
genpages = \
	kopano.7 \
	kopano-admin.8 \
	kopano-archiver.8 kopano-archiver.cfg.5 \
	kopano-autorespond.8 \
	kopano-dagent.8 kopano-dagent.cfg.5 \
	kopano-mr-accept.8 kopano-mr-process.8 \
	kopano-fsck.1 \
	kopano-gateway.8 kopano-gateway.cfg.5 \
	kopano-ical.8 kopano-ical.cfg.5 \
	kopano-ldap.cfg.5 \
	kopano-mailbox-permissions.8 \
	kopano-monitor.8 kopano-monitor.cfg.5 \
	kopano-passwd.1 \
	kopano-search.8 kopano-search.cfg.5 \
	kopano-server.8 kopano-server.cfg.5 \
	kopano-set-oof.1 \
	kopano-spooler.8 kopano-spooler.cfg.5 \
	kopano-backup.8 \
	kopano-stats.1 \
	kopano-unix.cfg.5

CLEANFILES = ${genpages} kopano.0
EXTRA_DIST = manual.xml

${genpages}: kopano.0

# Since the manual.xml file is in UTF-8, we need to call 'xmlto' in UTF-8 mode as well.
kopano.0: manual.xml
	@${AM_V_GEN} if [ -z "$(PROG_XMLTO)" ]; then \
		echo Please install xmlto to generate manual pages; \
	else \
		LC_ALL=en_US.UTF-8 $(PROG_XMLTO) man ${srcdir}/manual.xml && \
		touch $@; \
	fi
