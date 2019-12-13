--TEST--
mapi_ab_getdefaultdir() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_TEST_SERVER")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$session = getMapiSession();
$ab = mapi_openaddressbook($session);
var_dump(bin2hex(mapi_ab_getdefaultdir($ab)));
--EXPECTF--
string(72) "%s"
