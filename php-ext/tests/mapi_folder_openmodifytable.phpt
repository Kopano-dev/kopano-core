--TEST--
mapi_folder_openmodifytable() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_TEST_SERVER")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$store = getDefaultStore(getMapiSession());
$inbox = mapi_msgstore_getreceivefolder($store);

var_dump(mapi_folder_openmodifytable($inbox));
--EXPECTF--
resource(%d) of type (MAPI Exchange Modify Table)

