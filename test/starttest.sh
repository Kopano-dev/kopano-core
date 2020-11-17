#!/bin/sh

# This script is the main test runner. It is run inside the corresponding
# Docker container. Do not run directly, instad run `make test-backend-kopano-ci-run`.

set -eu

export PYTHONDONTWRITEBYTECODE=yes

PYTHON=${PYTHON:-python3}
PYTEST=${PYTEST:-py.test-3}
export KOPANO_SOCKET=${KOPANO_SOCKET:-file:///run/kopano/server.sock}
export KOPANO_TEST_USER=${KOPANO_TEST_USER:-user1}
export KOPANO_TEST_PASSWORD=${KOPANO_TEST_PASSWORD:-user1}
export KOPANO_TEST_EMAIL=${KOPANO_TEST_EMAIL:-user1@kopano.demo}
export KOPANO_TEST_USER2=${KOPANO_TEST_USER2:-user2}
export KOPANO_TEST_PASSWORD2=${KOPANO_TEST_PASSWORD2:-user2}
export KOPANO_TEST_USER3=${KOPANO_TEST_USER3:-user3}
export KOPANO_TEST_PASSWORD3=${KOPANO_TEST_PASSWORD3:-user3}
export KOPANO_TEST_USER4=${KOPANO_TEST_USER4:-user4}
export KOPANO_TEST_PASSWORD4=${KOPANO_TEST_PASSWORD4:-user4}
export KOPANO_TEST_FULLNAME3=${KOPANO_TEST_FULLNAME3:-"Marijn Peters"}
export KOPANO_TEST_ADMIN=${KOPANO_TEST_ADMIN:-user23}
export KOPANO_TEST_ADMIN_PASSWORD=${KOPANO_TEST_ADMIN_PASSWORD:-user23}
export KOPANO_TEST_POP3_USERNAME=${KOPANO_TEST_POP3_USERNAME:-user4}
export KOPANO_TEST_POP3_PASSWORD=${KOPANO_TEST_POP3_PASSWORD:-user4}
export KOPANO_TEST_IMAP_HOST=${KOPANO_TEST_IMAP_HOST:-kopano_gateway}
export KOPANO_TEST_POP3_HOST=${KOPANO_TEST_POP3_HOST:-kopano_gateway}
export KOPANO_TEST_DAGENT_HOST=${KOPANO_TEST_DAGENT_HOST:-kopano_dagent}

if [ "$CI" -eq "1" ]; then
	if [ -x "$(command -v dockerize)" ]; then
		dockerize -wait $KOPANO_SOCKET -timeout 60s
	fi
fi

count=0
while true; do
	if kopano-admin --sync; then
		break
	fi

	if [ "$count" -eq 10 ]; then
		exit 1
	fi

	count=$((count +1))
	sleep 1
done

# TODO(jelle): handle public store creation failure
kopano-admin -s || true

kopano-admin -l

exec make test-ci \
	PYTHON=$PYTHON \
	PYTEST=$PYTEST \
	KOPANO_SOCKET=$KOPANO_SOCKET
