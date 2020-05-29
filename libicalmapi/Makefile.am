KOPANO_TEST_USER ?= user1
KOPANO_TEST_PASSWORD ?= user1
PYTEST ?= pytest


.PHONY: test
test:
	KOPANO_TEST_USER=${KOPANO_TEST_USER} KOPANO_TEST_PASSWORD=${KOPANO_TEST_PASSWORD} \
	  $(PYTEST) tests/ --junitxml=test.xml -o junit_suite_name=icalmapi
