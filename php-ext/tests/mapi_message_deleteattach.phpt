--TEST--
mapi_message_deleteattach() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_TEST_SERVER")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$store = getDefaultStore(getMapiSession());
$root = mapi_msgstore_openentry($store, null);
$message = mapi_folder_createmessage($root);

var_dump(mapi_message_deleteattach($message, 0));
--EXPECTF--
bool(false)
