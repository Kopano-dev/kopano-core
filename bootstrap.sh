#!/bin/sh

if ! which pkg-config >/dev/null 2>/dev/null; then
	# If you do not have this, useless autoconf error messages
	# can result: possibly undefined macro AC_CHECK_LIB
	echo "pkg-config seems to be missing";
	exit 1
fi
exec autoreconf -fiv
