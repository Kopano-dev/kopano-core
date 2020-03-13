--TEST--
mapi_message_getattachmenttable() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$store = getDefaultStore(getMapiSession());
$root = mapi_msgstore_openentry($store, null);
$message = mapi_folder_createmessage($root);

var_dump(mapi_message_getattachmenttable($message));
--EXPECTF--
resource(%s) of type (MAPI Table)
