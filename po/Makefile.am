datadir = ${datarootdir}/locale/
EXTRA_DIST = bg.po ca.po cs.po da.po de.po el.po es.po et.po fa.po fi.po \
        fr.po fr_BE.po gl.po he.po hr.po hu.po it.po ja.po ko.po lt.po nb.po nl.po \
        nl_BE.po pl.po pt.po pt_BR.po ru.po sl.po sv.po tr.po uk.po zh_CN.po zh_TW.po

install-data-local:
	mkdir -p $(DESTDIR)$(datadir)
	for po in $(EXTRA_DIST); do \
		lang="$${po%.po}"; \
		sub="$(DESTDIR)$(datadir)/$$lang/LC_MESSAGES"; \
		mkdir -p $$sub; \
		msgfmt -f -v -o $$sub/kopano.mo ${top_srcdir}/po/$$po; \
	done
