# shell include script

PATH=/bin:/usr/local/bin:/usr/bin
export PATH

if [ -z "${KOPANO_GROUP_SCRIPTS}" ] ; then
    exec >&2
    echo "Do not execute this script directly"
    exit 1
fi

if [ ! -d "${KOPANO_GROUP_SCRIPTS}" ] ; then
    exec >&2
    echo "${KOPANO_GROUP_SCRIPTS} does not exist or is not a directory"
    exit 1
fi

if [ -z "${KOPANO_GROUP}" -a -z "${KOPANO_GROUPID}" ] ; then
    exec >&2
    echo "KOPANO_GROUP and KOPANO_GROUPID is not set."
    exit 1
fi
exec "$PKGLIBEXECDIR/kscriptrun" "$KOPANO_GROUP_SCRIPTS"
