--TEST--
mapi_openaddressbook() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_TEST_SERVER")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$session = getMapiSession();
$ab = mapi_openaddressbook($session);
var_dump($ab);
--EXPECTF--
resource(%d) of type (MAPI Addressbook)
