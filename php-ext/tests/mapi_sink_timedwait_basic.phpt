--TEST--
mapi_sink_timedwait() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_sink_timedwait(null, 0));
--EXPECTF--
Warning: mapi_sink_timedwait() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
