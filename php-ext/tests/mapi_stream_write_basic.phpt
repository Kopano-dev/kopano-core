--TEST--
mapi_stream_write() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || getenv('KOPANO_SOCKET')) print "skip"; ?>
--FILE--
<?php
$stream = mapi_stream_create();
var_dump(mapi_stream_write($stream, 'test'));
--EXPECT--
int(4)
