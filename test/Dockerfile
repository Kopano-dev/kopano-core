ARG docker_repo=kopano
ARG kopano_core_version=latest
FROM ${docker_repo}/kopano_core:${kopano_core_version}

ENV WORKSPACE=/workspace

WORKDIR ${WORKSPACE}

RUN apt-get update && apt-get install --no-install-recommends -y \
	autoconf \
	automake \
	build-essential \
	flake8 \
	pkg-config \
	python3-dateutil \
	python3-dev \
	python3-pillow \
	python3-pytest \
	python3-setuptools \
	python3-tz \
	python3-tzlocal \
	php

RUN rm -f /etc/apt/sources.list.d/kopano.list \
	&& dpkg -r --force-depends \
		kopano-common \
		kopano-grapi-bin \
		kopano-lang \
		libmapi1 \
		libkcutil0 \
		libkcserver0 \
		libkcinetmapi0 \
	&& apt-get install -f -y

ENV LD_LIBRARY_PATH=${WORKSPACE}/.libs:${WORKSPACE}/swig/python/.libs
ENV PYTHONPATH=${WORKSPACE}/swig/python:${WORKSPACE}/swig/python/.libs:${WORKSPACE}/swig/python/kopano/
env MAPI_CONFIG_PATH=${WORKSPACE}/provider/client

# NOTE(longsleep): Always link kopano-admin to make it available for the test
# container which is not run as root, and thus cannot create this itself. This
# avoids a su-exec step in the test container and is fine until there are a
# lof of extra tools required to be in /usr/sbin.
RUN ln -svnf $WORKSPACE/.libs/kopano-admin /usr/sbin/

CMD [ "/bin/true" ]
