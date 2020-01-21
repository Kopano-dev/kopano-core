--TEST--
mapi_freebusydata_getpublishrange() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_TEST_SERVER")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$session = getMapiSession();
$store = getDefaultStore($session);
$fb = mapi_freebusysupport_open($session);
$props = mapi_getprops($store, array(PR_MAILBOX_OWNER_ENTRYID));

$update = mapi_freebusysupport_loadupdate($fb, $props);
$data = mapi_freebusysupport_loaddata($fb, array($props[PR_MAILBOX_OWNER_ENTRYID], ''));

var_dump(mapi_freebusydata_getpublishrange($data[0]));
--EXPECTF--
array(2) {
  ["start"]=>
  int(-%d)
  ["end"]=>
  int(-%d)
}
