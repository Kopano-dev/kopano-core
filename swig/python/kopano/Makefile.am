EXTRA_DIST = changelog.rst requirements.txt setup.cfg setup.py \
	kopano/__init__.py kopano/address.py kopano/appointment.py \
	kopano/attachment.py kopano/attendee.py kopano/autoaccept.py \
	kopano/autoprocess.py kopano/company.py kopano/compat.py \
	kopano/config.py kopano/contact.py kopano/defs.py \
	kopano/delegation.py kopano/distlist.py \
	kopano/errors.py kopano/folder.py \
	kopano/freebusy.py kopano/group.py kopano/ics.py kopano/item.py \
	kopano/log.py \
	kopano/meetingrequest.py kopano/notification.py kopano/outofoffice.py \
	kopano/parse.py kopano/parser.py kopano/permission.py \
	kopano/picture.py kopano/pidlid.py kopano/properties.py \
	kopano/property_.py kopano/query.py kopano/quota.py \
	kopano/recurrence.py kopano/restriction.py kopano/rule.py \
	kopano/server.py kopano/service.py kopano/store.py kopano/table.py \
	kopano/timezone.py kopano/user.py kopano/utils.py \
	kopano/version.py

PYTEST ?= pytest
PYTEST_COVERAGE_OPTIONS=--cov-report=term-missing --cov-report=html:tests/coverage --cov=kopano

install-exec-local:
	rm -Rf staging
	${MKDIR_P} staging
	cp -a ${srcdir}/setup.py ${srcdir}/setup.cfg ${srcdir}/kopano staging/
	cp -a kopano/version.py staging/kopano/
	cd staging/ && ${PYTHON} setup.py \
		build --build-base="${abs_builddir}/build" \
		install -f $${DESTDIR+--root=${DESTDIR}} \
		--prefix="${prefix}" --install-purelib="${pythondir}"
	rm -Rf build staging

uninstall-local:
	rm -Rf ${DESTDIR}${pythondir}/kopano \
		${DESTDIR}${pythondir}/kopano-*.egg-info

.PHONY: test
test:
	MAPI_CONFIG_PATH=../../../provider/client PYTHONPATH=../.libs:..:. ${PYTEST} --junitxml=test.xml .

.PHONY: test-coverage
test-coverage:
	MAPI_CONFIG_PATH=../../../provider/client PYTHONPATH=../.libs:..:. ${PYTEST} ${PYTEST_COVERAGE_OPTIONS} --junitxml=test.xml .
