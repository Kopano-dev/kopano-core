--TEST--
mapi_msgstore_advise() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$store = getDefaultStore(getMapiSession(0));
$inbox = mapi_msgstore_getreceivefolder($store);
$feid = mapi_getprops($inbox, array(PR_ENTRYID));
$sink = mapi_sink_create();
$advisesink = mapi_msgstore_advise($store, $feid[PR_ENTRYID], fnevObjectCreated, $sink);
var_dump($advisesink);
mapi_msgstore_unadvise($store, $advisesink);
--EXPECTF--
int(%d)
