--TEST--
mapi_table_restrict() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$session = getMapiSession();
$store = getDefaultStore($session);
$root = mapi_msgstore_openentry($store);
$table = mapi_folder_gethierarchytable($root);
var_dump(mapi_table_restrict($table, array(RES_CONTENT,
							array(
								 FUZZYLEVEL      =>      FL_PREFIX|FL_IGNORECASE,
								 ULPROPTAG       =>      PR_CONTAINER_CLASS,
								 VALUE           =>      array(PR_CONTAINER_CLASS => "NOTHING")
								 )
			     ), 0));

--EXPECT--
bool(true)
