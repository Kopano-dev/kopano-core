# -*- Makefile -*-

EXTRA_DIST = \
	COMPAT.md README.md TODO.txt kopano-mfr.py requirements.txt setup.cfg \
	setup.py wsgi_profiler_conf.py \
	kopano_rest/__init__.py \
	kopano_rest/__main__.py \
	kopano_rest/utils.py \
	kopano_rest/api_v1/config.py \
	kopano_rest/api_v1/__init__.py \
	kopano_rest/api_v1/notify.py \
	kopano_rest/api_v1/rest.py \
	kopano_rest/resource/attachment.py \
	kopano_rest/resource/calendar.py \
	kopano_rest/resource/contactfolder.py \
	kopano_rest/resource/contact.py \
	kopano_rest/resource/event.py \
	kopano_rest/resource/folder.py \
	kopano_rest/resource/group.py \
	kopano_rest/resource/__init__.py \
	kopano_rest/resource/item.py \
	kopano_rest/resource/mailfolder.py \
	kopano_rest/resource/message.py \
	kopano_rest/resource/profilephoto.py \
	kopano_rest/resource/reminder.py \
	kopano_rest/resource/resource.py \
	kopano_rest/resource/schema.py \
	kopano_rest/resource/user.py

if WITH_PYTHON3

install-exec-local:
	rm -Rf staging
	${MKDIR_P} staging
	cp -a ${srcdir}/setup.py ${srcdir}/setup.cfg ${srcdir}/kopano_rest staging/
	cp -a kopano_rest/version.py staging/kopano_rest/
	cd staging/ && ${PYTHON} setup.py \
		build --build-base="${abs_builddir}/build" \
		install -f $${DESTDIR+--root=${DESTDIR}} \
		--prefix="${prefix}" --install-purelib="${pythondir}"
	rm -Rf build

uninstall-local:
	rm -Rf ${DESTDIR}${pythondir}/kopano_rest \
		${DESTDIR}${pythondir}/kopano_rest-*.egg-info

endif
