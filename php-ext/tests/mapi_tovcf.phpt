--TEST--
mapi_mapitovcf() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_TEST_SERVER")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');
$session = getMapiSession();
$store = getDefaultStore($session);
$ab = mapi_openaddressbook($session);
$root = mapi_msgstore_openentry($store, null);
$message = mapi_folder_createmessage($root);
mapi_setprops($message, array(PR_SUBJECT => "Test"));
mapi_message_savechanges($message);
var_dump(mapi_mapitovcf(null, $ab, $message, array()));

/// TODO: add succesfull export
--EXPECTF--
bool(false)
