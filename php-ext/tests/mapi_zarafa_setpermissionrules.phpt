--TEST--
mapi_zarafa_setpermissionrules() server tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$store = getDefaultStore(getMapiSession());
$root = mapi_msgstore_openentry($store, null);

var_dump(mapi_zarafa_setpermissionrules($root, array()));

$props = mapi_getprops($store, array(PR_MAILBOX_OWNER_ENTRYID));
$data = mapi_zarafa_getgrouplistofuser($store, $props[PR_MAILBOX_OWNER_ENTRYID]);
$eeid = $data["Everyone"]["groupid"];

$data = array();
$data[0]["userid"] = $eeid;
$data[0]["type"] = ACCESS_TYPE_GRANT;
$data[0]["rights"] = ecRightsFullControl;
$data[0]["state"] = RIGHT_NEW | RIGHT_AUTOUPDATE_DENIED;
var_dump(mapi_zarafa_setpermissionrules($store, $data));
--EXPECTF--
bool(true)
