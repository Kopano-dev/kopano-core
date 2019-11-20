--TEST--
mapi_Table_getrowcount() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_TEST_SERVER")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$session = getMapiSession();
$store = getDefaultStore($session);
$root = mapi_msgstore_openentry($store);
$table = mapi_folder_getcontentstable($root);
var_dump(gettype(mapi_table_getrowcount($table)));
--EXPECT--
string(7) "integer"
