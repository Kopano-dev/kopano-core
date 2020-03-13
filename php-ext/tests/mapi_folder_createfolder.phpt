--TEST--
mapi_folder_createfolder() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$store = getDefaultStore(getMapiSession());
$root = mapi_msgstore_openentry($store, null);
var_dump(mapi_folder_createfolder($root, 'newfolder', '', OPEN_IF_EXISTS));
--EXPECTF--
resource(%d) of type (MAPI Folder)
