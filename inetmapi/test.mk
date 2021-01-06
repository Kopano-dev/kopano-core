INETMAPI_DIR:=$(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))

MAPI_CONFIG_PATH = ${INETMAPI_DIR}/../provider/client
PYTHONPATH = ${INETMAPI_DIR}/../swig/python/.libs:${INETMAPI_DIR}/../swig/python/
KOPANO_TEST_USER ?= user1
KOPANO_TEST_PASSWORD ?= user1
PYTEST ?= pytest


.PHONY: test
test:
	MAPI_CONFIG_PATH=${MAPI_CONFIG_PATH} PYTHONPATH=${PYTHONPATH}  \
	KOPANO_TEST_USER=${KOPANO_TEST_USER} KOPANO_TEST_PASSWORD=${KOPANO_TEST_PASSWORD} \
	$(PYTEST) ${INETMAPI_DIR}/tests/ --junitxml=${INETMAPI_DIR}/test.xml -o junit_suite_name=inetmapi
