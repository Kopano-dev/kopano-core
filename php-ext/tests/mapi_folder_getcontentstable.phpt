--TEST--
mapi_folder_getcontentstable() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_TEST_SERVER")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$session = getMapiSession();
$store = getDefaultStore($session);
$root = mapi_msgstore_openentry($store);
var_dump(mapi_folder_getcontentstable($root));
--EXPECTF--
resource(%d) of type (MAPI Table)
