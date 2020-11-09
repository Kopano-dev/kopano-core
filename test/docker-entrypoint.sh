#!/bin/sh

set -e

WORKSPACE=${WORKSPACE:-$(pwd)}

export LD_LIBRARY_PATH=${WORKSPACE}/.libs:${WORKSPACE}/swig/python/.libs
export PYTHONPATH=${WORKSPACE}/swig/python:${WORKSPACE}/swig/python/.libs:${WORKSPACE}/swig/python/kopano/
export MAPI_CONFIG_PATH=${WORKSPACE}/provider/client:${WORKSPACE}/provider/contacts

if [ -f /kopano/start-service.sh ]; then
	set -- /kopano/start-service.sh "$@"
fi

exec "$@"
