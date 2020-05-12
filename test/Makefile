WORKSPACE ?= /workspace
DOCKERCOMPOSE ?= docker-compose
DOCKERCOMPOSE_UP_ARGS ?= --build
DOCKERCOMPOSE_EXEC_ARGS ?= -T -u $(shell id -u) -e HOME=$(WORKSPACE) -e WORKSPACE=$(WORKSPACE)
EXTRA_LOCAL_ADMIN_USER ?= $(shell id -u)

BUILD_TAG ?= $(shell whoami || date +%s)

export COMPOSE_PROJECT_NAME=kopanocore-test-$(BUILD_TAG)
export EXTRA_LOCAL_ADMIN_USER

test: test-python-mapi test-python-kopano test-php test-gateway
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

# CI

.PHONY: test-backend-kopano-ci-run
test-backend-kopano-ci-run:
	$(DOCKERCOMPOSE) up -d $(DOCKERCOMPOSE_UP_ARGS)
	$(DOCKERCOMPOSE) exec $(DOCKERCOMPOSE_EXEC_ARGS) kopano_server_test $(WORKSPACE)/test/starttest.sh

.PHONY: test-backend-kopano-ci-clean
test-backend-kopano-ci-clean:
	$(DOCKERCOMPOSE) down -v || true
