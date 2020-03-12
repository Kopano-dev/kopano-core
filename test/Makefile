DOCKERCOMPOSE ?= docker-compose
DOCKERCOMPOSE_UP_ARGS ?= --build
DOCKERCOMPOSE_EXEC_ARGS ?= -T -u $(shell id -u) -e HOME=/workspace
EXTRA_LOCAL_ADMIN_USER ?= $(shell id -u)

#BUILD_TAG ?= $(shell date +%s)
BUILD_TAG ?= $(shell whoami)

export COMPOSE_PROJECT_NAME=kopanocore-test-$(BUILD_TAG)
export EXTRA_LOCAL_ADMIN_USER

# TODO(longsleep): Add tests here.
test:

# CI

.PHONY: test-backend-kopano-ci-run
test-backend-kopano-ci-run:
	$(DOCKERCOMPOSE) up -d $(DOCKERCOMPOSE_UP_ARGS)
	$(DOCKERCOMPOSE) exec $(DOCKERCOMPOSE_EXEC_ARGS) kopano_server_test /workspace/test/starttest.sh

.PHONY: test-backend-kopano-ci-clean
test-backend-kopano-ci-clean:
	$(DOCKERCOMPOSE) down -v || true
