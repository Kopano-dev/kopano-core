--TEST--
mapi_rules_gettable() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$store = getDefaultStore(getMapiSession());
$inbox = mapi_msgstore_getreceivefolder($store);
$table = mapi_folder_openmodifytable($inbox);

var_dump(mapi_rules_gettable($table));
--EXPECTF--
resource(%d) of type (MAPI Table)
