#!/bin/sh

# This script is the startup script for the kopano core container to be used
# when running the CI tests.

set -eu

if [ -n "$EXTRA_LOCAL_ADMIN_USER" ]; then
	useradd \
		--no-create-home \
		--no-user-group \
		--uid $EXTRA_LOCAL_ADMIN_USER \
		testrunner || true
fi

ln -svnf $WORKSPACE/.libs/kopano-* /usr/sbin/

mkdir -p /usr/local/libexec/kopano
ln -svnf $WORKSPACE/.libs/kscriptrun /usr/local/libexec/kopano/

mkdir -p /usr/share/kopano
ln -svnf $WORKSPACE/installer/linux/ldap.openldap.cfg /usr/share/kopano
ln -svnf $WORKSPACE/installer/linux/ldap.propmap.cfg /usr/share/kopano

ln -svnf $WORKSPACE/installer/userscripts/ /usr/lib/kopano

exec /kopano/start-service.sh
