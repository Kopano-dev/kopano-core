--TEST--
mapi_folder_gethierarchytable() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_TEST_SERVER")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$session = getMapiSession();
$ab = mapi_openaddressbook($session);
$root = mapi_ab_openentry($ab);
var_dump(mapi_folder_gethierarchytable($root));
--EXPECTF--
resource(%d) of type (MAPI Table)
