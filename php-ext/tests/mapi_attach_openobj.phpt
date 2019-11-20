--TEST--
mapi_attach_openobj() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_TEST_SERVER")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$store = getDefaultStore(getMapiSession());
$root = mapi_msgstore_openentry($store, null);
$message = mapi_folder_createmessage($root);
$attach = mapi_message_createattach($message);

var_dump(mapi_attach_openobj($attach, MAPI_CREATE));
--EXPECTF--
resource(%d) of type (MAPI Message)
