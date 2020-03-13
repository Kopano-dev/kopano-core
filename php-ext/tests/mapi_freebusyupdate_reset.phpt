--TEST--
mapi_freebusyupdate_reset() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$session = getMapiSession();
$store = getDefaultStore($session);
$fb = mapi_freebusysupport_open($session);
$props = mapi_getprops($store, array(PR_MAILBOX_OWNER_ENTRYID));

$update = mapi_freebusysupport_loadupdate($fb, $props);
$data = mapi_freebusysupport_loaddata($fb, array($props[PR_MAILBOX_OWNER_ENTRYID], ''));

var_dump(mapi_freebusyupdate_reset($update[0]), array());
--EXPECTF--
bool(true)
array(0) {
}
