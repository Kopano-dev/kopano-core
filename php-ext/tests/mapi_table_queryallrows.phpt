--TEST--
mapi_table_queryallrows() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$session = getMapiSession();
$ab = mapi_openaddressbook($session);
$root = mapi_ab_openentry($ab);
$table = mapi_folder_gethierarchytable($root);
var_dump(gettype(mapi_table_queryallrows($table)));
--EXPECT--
string(5) "array"
