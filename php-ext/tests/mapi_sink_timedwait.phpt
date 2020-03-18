--TEST--
mapi_sink_timedwait() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
$sink = mapi_sink_create();
var_dump(mapi_sink_timedwait($sink, 1));
--EXPECT--
array(0) {
}
