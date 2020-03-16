--TEST--
mapi_sink_create() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_sink_create());
--EXPECTF--
resource(%d) of type (MAPI Advise sink)
