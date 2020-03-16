--TEST--
mapi_stream_create() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
var_dump(mapi_stream_create());
--EXPECTF--
resource(%d) of type (IStream Interface)
