--TEST--
mapi_ab_openentry() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_TEST_SERVER")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$session = getMapiSession();
$ab = mapi_openaddressbook($session);
var_dump(mapi_ab_openentry($ab, null));
$root = mapi_ab_openentry($ab);
var_dump($root);
--EXPECTF--
bool(false)
resource(%d) of type (MAPI Addressbook Container)
