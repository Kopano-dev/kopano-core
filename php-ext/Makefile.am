PHP = /usr/bin/php
KOPANO_TEST_USER ?= user1
KOPANO_TEST_PASSWORD ?= user1
MAPI_CONFIG_PATH = ${top_srcdir}/provider/client
TEST_PHP_JUNIT ?= test.log

.PHONY: test
test:
	MAPI_CONFIG_PATH=${MAPI_CONFIG_PATH}
	TEST_PHP_ARGS="-q" \
		KOPANO_TEST_USER=$(KOPANO_TEST_USER) \
		KOPANO_TEST_PASSWORD=$(KOPANO_TEST_PASSWORD) \
		TEST_PHP_JUNIT=$(TEST_PHP_JUNIT) \
		${PHP} ${srcdir}/tests/run-tests.php -p ${PHP} -n \
		-d extension_dir=${top_builddir}/.libs/ -d extension=mapi.so --show-diff
