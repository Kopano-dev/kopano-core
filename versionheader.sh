#!/bin/sh

set -e

# created by make, unmodified $PACKAGE_VERSION
acver=$(cat .version)
if test -z "$acver"; then
	>&2 echo "Missing the .version file as created by make."
	exit 1
fi
# created by user/buildservice, with desired suffixes
version=$(cat version 2>/dev/null || :)
if test -z "$version"; then version="$acver"; fi
if perl -e 'exit!($ARGV[1]!~/^\Q$ARGV[0]\E\b/)' "$acver" "$version"; then
	echo "The source code version is \"$acver\" (as defined by configure.ac)." >&2
	echo "The \"version\" file can be used to override this, HOWEVER, it is only meant" >&2
	echo "for _appending_ suffixes, not change it to something entirely different." >&2
	echo "Current contents of \"version\": \"$version\"" >&2
	echo "If in doubt, remove the \"version\" file before retrying." >&2
	exit 1
fi
set -- $(echo "$version" | sed -e 's/ .*//g;s/[^0-9a-z][^0-9a-z]*/ /g' 2>/dev/null)
major_version="$1"
minor_version="$2"
micro_version="$3"
localrev="$4"
if test -z "$major_version"; then major_version=0; fi
if test -z "$minor_version"; then minor_version=0; fi
if test -z "$micro_version"; then micro_version=0; fi
if test -z "$localrev"; then localrev=0; fi

cat << EOF
#define PROJECT_VERSION                 "$version"
#define PROJECT_VERSION_MAJOR           $major_version
#define PROJECT_VERSION_MINOR           $minor_version
#define PROJECT_VERSION_MICRO           $micro_version
#define PROJECT_VERSION_REVISION        ${localrev}UL
#define PROJECT_VERSION_COMMIFIED       "$major_version,$minor_version,$micro_version"
EOF
