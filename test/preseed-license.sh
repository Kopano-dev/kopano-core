#!/bin/sh

set -e

if [ -n "${KUSTOMERD_PRESEED_LICENSE}" ]; then
    echo "${KUSTOMERD_PRESEED_LICENSE}" > /etc/kopano/licenses/preseed-license
fi

exec su-exec nobody:nogroup "$@"
