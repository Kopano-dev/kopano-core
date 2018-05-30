# shell include script

KOPANO_LANG="${KOPANO_USERSCRIPT_LOCALE:-${LC_MESSAGES:-C}}"
PATH=/bin:/usr/local/bin:/usr/bin
export KOPANO_LANG PATH

if [ -z "${KOPANO_USER_SCRIPTS}" ] ; then
    exec >&2
    echo "Do not execute this script directly"
    exit 1
fi

if [ ! -d "${KOPANO_USER_SCRIPTS}" ] ; then
    exec >&2
    echo "${KOPANO_USER_SCRIPTS} does not exist or is not a directory"
    exit 1
fi

if [ -z "${KOPANO_USER}" -a -z "${KOPANO_STOREGUID}" ] ; then
    exec >&2
    echo "KOPANO_USER and KOPANO_STOREGUID is not set."
    exit 1
fi

# Find cannot cope with unreadable cwd
cd "$KOPANO_USER_SCRIPTS"
find -L "$KOPANO_USER_SCRIPTS"/* -maxdepth 0 -type f -perm -u=x ! -name \*~ ! -name \#\* ! -name \*.rpm\* ! -name \*.bak ! -name \*.old -exec {} \;
