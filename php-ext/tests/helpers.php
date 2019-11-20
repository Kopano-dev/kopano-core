<?php
require(__DIR__."/mapidef.php");

function getMapiSession($notifications=1) {
	return mapi_logon_zarafa(getenv("KOPANO_TEST_USER"), getenv("KOPANO_TEST_PASSWORD"), getenv("KOPANO_TEST_SERVER"), null, null, $notifications);
}

function getDefaultStore($session)
{
    $msgstorestable = mapi_getmsgstorestable($session);
    $msgstores = mapi_table_queryallrows($msgstorestable, array(PR_DEFAULT_STORE, PR_ENTRYID));

    foreach ($msgstores as $row) {
        if($row[PR_DEFAULT_STORE]) {
            $storeentryid = $row[PR_ENTRYID];
            break;
        }
    }

    if (!$storeentryid) {
        return false;
    }

    $store = mapi_openmsgstore($session, $storeentryid);

    return $store;
}

function makeGuid($guid)
{
        return pack("vvvv", hexdec(substr($guid, 5, 4)), hexdec(substr($guid, 1, 4)), hexdec(substr($guid, 10, 4)), hexdec(substr($guid, 15, 4))) . hex2bin(substr($guid, 20, 4)) . hex2bin(substr($guid, 25, 12));
}
?>
