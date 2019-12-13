--TEST--
mapi_freebusyupdate_savechanges() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_freebusyupdate_savechanges(null, 0, 0));
--EXPECTF--
Warning: mapi_freebusyupdate_savechanges() expects parameter 1 to be resource, null given in %s on line %d
bool(false)
