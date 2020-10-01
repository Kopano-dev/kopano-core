#!/bin/sh

# This script is the startup script for the kopano core container to be used
# when running the CI tests.

set -eu

if [ "$SERVICE_TO_START" = "server" ]; then
	if grep -q "#define HAVE_KUSTOMER 1" "./common/config.h" ; then
		echo "Supported build detected."
		# This section contains extra environment variables which are set based
		# on if this is testing a supported build or not.

		#export KCCONF_SERVER_KUSTOMER_INITIALIZE_TIMEOUT=0 # allow startup without kustomerd for now
	fi
fi

WORKSPACE=${WORKSPACE:-$(pwd)}

export LD_LIBRARY_PATH=${WORKSPACE}/.libs:${WORKSPACE}/swig/python/.libs
export PYTHONPATH=${WORKSPACE}/swig/python:${WORKSPACE}/swig/python/.libs:${WORKSPACE}/swig/python/kopano/
export MAPI_CONFIG_PATH=${WORKSPACE}/provider/client

if [ -n "$EXTRA_LOCAL_ADMIN_USER" ]; then
	useradd \
		--no-create-home \
		--no-user-group \
		--uid $EXTRA_LOCAL_ADMIN_USER \
		testrunner || true
fi

ln -svnf $WORKSPACE/.libs/kopano-* /usr/sbin/

mkdir -p /usr/libexec/kopano
ln -svnf $WORKSPACE/kscriptrun /usr/libexec/kopano/

mkdir -p /usr/share/kopano
ln -svnf $WORKSPACE/installer/linux/ldap.openldap.cfg /usr/share/kopano/
ln -svnf $WORKSPACE/installer/linux/ldap.propmap.cfg /usr/share/kopano/

ln -svnf $WORKSPACE/installer/userscripts/ /usr/lib/kopano/
mkdir -p /etc/kopano/userscripts/createuser.d
ln -svnf $WORKSPACE/installer/userscripts/00createstore /etc/kopano/userscripts/createuser.d/

mkdir -p /usr/local/libexec && ln -svnf /usr/libexec/kopano /usr/local/libexec/
mkdir -p /usr/local/lib && ln -svnf /usr/lib/kopano /usr/local/lib/
mkdir -p /usr/local/etc && ln -svnf /etc/kopano /usr/local/etc/

exec /kopano/start-service.sh
