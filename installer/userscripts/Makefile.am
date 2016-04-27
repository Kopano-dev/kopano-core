SUBDIRS = createcompany.d creategroup.d createuser.d deletecompany.d deletegroup.d deleteuser.d
dist_bin_SCRIPTS = createcompany creategroup createuser deletecompany deletegroup deleteuser
bindir = $(USERSCRIPTDIR)
dist_data_DATA = users_common.sh groups_common.sh companies_common.sh
datadir = $(USERSCRIPTDIR)

install-data-hook:
	for dir in $(SUBDIRS); do \
		mkdir -p $(DESTDIR)/$(USERSCRIPTDIR)/$$dir; \
	done
