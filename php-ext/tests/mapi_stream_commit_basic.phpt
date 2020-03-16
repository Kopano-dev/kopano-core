--TEST--
mapi_stream_commit() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
$stream = mapi_stream_create();
mapi_stream_write($stream, "test");
var_dump(mapi_stream_commit($stream));
--EXPECT--
bool(true)
