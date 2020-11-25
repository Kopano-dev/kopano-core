WORKSPACE ?= /workspace
DOCKERCOMPOSE ?= docker-compose
DOCKERCOMPOSE_BUILD_ARGS ?= --parallel
DOCKERCOMPOSE_UP_ARGS ?= --build
DOCKERCOMPOSE_EXEC_ARGS ?= -T -u $(shell id -u) -e HOME=$(WORKSPACE) -e WORKSPACE=$(WORKSPACE)
DOCKERCOMPOSE_LOGS_ARGS ?=
EXTRA_LOCAL_ADMIN_USER ?= $(shell id -u)
KUSTOMERD_PRESEED_LICENSE ?=
TEST_LICENSE_AVAILABLE ?=

BUILD_TAG ?= $(shell whoami || date +%s)

TAP2JUNIT ?= tap2junit
BATS ?= bats

export COMPOSE_PROJECT_NAME=kopanocore-test-$(BUILD_TAG)
export EXTRA_LOCAL_ADMIN_USER
export KUSTOMERD_PRESEED_LICENSE
export TEST_LICENSE_AVAILABLE

test: test-python-mapi test-python-kopano test-php test-gateway test-libicalmapi test-spooler test-spooler-plugins test-admin
test-ci: test-python-mapi test-python-kopano test-php test-gateway test-libicalmapi test-spooler test-spooler-plugins test-admin-junit test-ectools-junit
test-short: test-short-python-mapi test-short-python-kopano test-short-php


test-python-mapi:
	make -C ../swig/python test

test-python-kopano:
	make -C ../swig/python/kopano/ test

test-short-python-mapi:
	make -C ../swig/python test-short

test-short-python-kopano:
	make -C ../swig/python/kopano/ test-short

test-short-php:
	make -C ../php-ext test TEST_PHP_JUNIT=test-short.log

test-php:
	make -C ../php-ext test

test-gateway:
	make -C ../gateway test

test-libicalmapi:
	make -C ../libicalmapi test

test-spooler:
	make -f ../spooler/test.mk

test-spooler-plugins:
	make -C ../spooler/python test

test-admin:
	$(BATS) ../ECtools/admin/tests

test-admin-junit:
	$(BATS) ../ECtools/admin/tests > test-admin.log || true
	$(TAP2JUNIT) test-admin.log

test-ectools:
	$(BATS) ../ECtools/tests

test-ectools-junit:
	$(BATS) ../ECtools/tests > test-ectools.log || true
	$(TAP2JUNIT) test-ectools.log

# CI

.test-backend-kopano-ci-run-logger.pid:
	($(MAKE) test-backend-kopano-ci-logs DOCKERCOMPOSE_LOGS_ARGS="--follow $(DOCKERCOMPOSE_LOGS_ARGS)" & echo $$! > $@)

.PHONY: test-backend-kopano-ci-run-start-logger
test-backend-kopano-ci-run-start-logger: .test-backend-kopano-ci-run-logger.pid

.PHONY: test-backend-kopano-ci-run-stop-logger
test-backend-kopano-ci-run-stop-logger: .test-backend-kopano-ci-run-logger.pid
	@kill `cat $<` 2>/dev/null || true
	@rm $< || true

.PHONY: test-backend-kopano-ci-build
test-backend-kopano-ci-build:
	$(DOCKERCOMPOSE) build $(DOCKERCOMPOSE_BUILD_ARGS)

.PHONY: test-backend-kopano-ci-up
test-backend-kopano-ci-up:
	$(DOCKERCOMPOSE) up -d $(DOCKERCOMPOSE_UP_ARGS)

.PHONY: test-backend-kopano-ci-run
test-backend-kopano-ci-run: test-backend-kopano-ci-up
	$(DOCKERCOMPOSE) exec $(DOCKERCOMPOSE_EXEC_ARGS) kopano_server_test $(WORKSPACE)/test/starttest.sh

.PHONY: test-backend-kopano-ci-logs
test-backend-kopano-ci-logs:
	$(DOCKERCOMPOSE) logs $(DOCKERCOMPOSE_LOGS_ARGS)

.PHONY: test-backend-kopano-ci-clean
test-backend-kopano-ci-clean:
	$(DOCKERCOMPOSE) down -v || true
