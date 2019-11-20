--TEST--
mapi_message_modifyrecipients() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_TEST_SERVER")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$store = getDefaultStore(getMapiSession());
$root = mapi_msgstore_openentry($store, null);
$message = mapi_folder_createmessage($root);

var_dump(mapi_message_modifyrecipients($message, MODRECIP_ADD, array(array(PR_DISPLAY_NAME => 'test'))));
--EXPECT--
bool(true)
