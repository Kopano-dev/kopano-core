--TEST--
mapi_folder_setsearchcriteria() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$store = getDefaultStore(getMapiSession());
$root = mapi_msgstore_openentry($store, null);

var_dump(mapi_folder_setsearchcriteria($root, array(), array(), 0));
--EXPECTF--
Warning: mapi_folder_setsearchcriteria(): Wrong array should be array(RES_, array(values)) in %s on line %d
bool(false)
