datadir = ${datarootdir}/locale/
EXTRA_DIST = ca.po cs.po da.po de.po es.po et.po fa.po fi.po \
        fr.po fr_BE.po he.po hr.po hu.po it.po ja.po nb.po nl.po \
        nl_BE.po pl.po pt.po pt_BR.po ru.po sl.po sv.po tr.po zh_CN.po zh_TW.po

install-data-local:
	mkdir -p $(DESTDIR)$(datadir)
	for po in $(EXTRA_DIST); do \
		lang="$${po%.po}"; \
		sub="$(DESTDIR)$(datadir)/$$lang/LC_MESSAGES"; \
		mkdir -p $$sub; \
		msgfmt -f -v -o $$sub/kopano.mo ${top_srcdir}/po/$$po; \
	done

uninstall-local:
	for po in ${EXTRA_DIST}; do \
		rm -f "${DESTDIR}${datadir}/$${po%.po}/LC_MESSAGES/kopano.mo"; \
	done
