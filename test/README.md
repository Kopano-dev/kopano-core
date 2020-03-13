# Kopano Core test suite

This test directory contains the test suite for automated testing in a defined
reproducible environment using Docker and Docker Compose.

## Requirements

- Docker
- Docker Compose >= 1.25

## Build

The tests run whatever is build in the current repository. Since the test
environment is Debian 10, the local build must also be Debian 10. A simple
way to do this is to use the `Dockerfile.build` at the top of this repository
and build with that.

First build the builder:

```
docker build . -f Dockerfile.build -t kopanocore-builder
```

Then build using this builder image:

```
docker run -it --rm -v $(pwd):/build -u $(id -u) kopanocore-builder
```

You need to do this only once / after each change to the software.

## Run test suite

In the `test` folder run the test suite in the Docker Compose environment like
this:

```
make test-backend-kopano-ci-run
```

This creates a full featured test environment with services and runs tests
automatically (via top level `make test`). The environment keeps running so it
can be inspected / reused quickly.

## Shut down test suite

In the `test` folder to shut down and purge all data created from the test
suite, run the following:

```
make test-backend-kopano-ci-clean
```

## Manually run docker-compose

In the `test` folder, Docker Compose can be used to interact with the
containers of the test suite. You need to set an environment variable to let
Docker Compose know what your containers are named (since they are conflict
free by default).

When run manually use `export COMPOSE_PROJECT_NAME=kopanocore-test-$(whoami)` as
this is what is also used by the Makefile by default.

Then docker-compose can be used just normal.

```
docker-compose ps                                                         Name                                   Command                   State             Ports
-------------------------------------------------------------------------------------------------------------------
kopanocore-test-longsleep_db_1                   docker-entrypoint.sh mysqld      Up (healthy)     3306/tcp
kopanocore-test-longsleep_kopano_server_1        /prepare-and-start-service.sh    Up (unhealthy)
kopanocore-test-longsleep_kopano_server_test_1   /usr/bin/dumb-init -- /kop ...   Up (unhealthy)
kopanocore-test-longsleep_kopano_ssl_1           /start.sh                        Exit 0
kopanocore-test-longsleep_ldap_1                 /container/tool/run --logl ...   Up               389/tcp, 636/tcp
```

Other commands you might find useful:

```
docker-compose exec kopano_server bash
docker-compose exec kopano_server_test bash
docker-compose logs -f kopano_server
docker-compose logs -f db
```

