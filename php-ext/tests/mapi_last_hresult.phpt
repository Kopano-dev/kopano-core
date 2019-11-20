--TEST--
mapi_last_hresult() tests
--SKIPIF--
<?php if (!extension_loaded("mapi")) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_last_hresult());
--EXPECT--
int(0)
