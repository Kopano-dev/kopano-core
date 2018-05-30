# shell include script

KOPANO_LANG="${KOPANO_USERSCRIPT_LOCALE:-${LC_MESSAGES:-en_US}}"
PATH=/bin:/usr/local/bin:/usr/bin
export KOPANO_LANG PATH

if [ -z "${KOPANO_COMPANY_SCRIPTS}" ] ; then
    exec >&2
    echo "Do not execute this script directly"
    exit 1
fi

if [ ! -d "${KOPANO_COMPANY_SCRIPTS}" ] ; then
    exec >&2
    echo "${KOPANO_COMPANY_SCRIPTS} does not exist or is not a directory"
    exit 1
fi

if [ -z "${KOPANO_COMPANY}" -a -z "${KOPANO_COMPANYID}" ] ; then
    exec >&2
    echo "KOPANO_COMPANY and KOPANO_COMPANYID is not set."
    exit 1
fi

find -L ${KOPANO_COMPANY_SCRIPTS} -maxdepth 1 -type f -perm -u=x ! -name \*~ ! -name \#\* ! -name \*.rpm\* ! -name \*.bak ! -name \*.old -exec {} \;
