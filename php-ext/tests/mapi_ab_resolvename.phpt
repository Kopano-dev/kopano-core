--TEST--
mapi_ab_resolvename() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$session = getMapiSession();
$ab = mapi_openaddressbook($session);
$root = mapi_ab_openentry($ab);
var_dump(mapi_ab_resolvename($ab, array()));
var_dump(mapi_ab_resolvename($ab, array(array(PR_DISPLAY_NAME => "NOTHING"))));
--EXPECTF--
array(0) {
}
bool(false)
