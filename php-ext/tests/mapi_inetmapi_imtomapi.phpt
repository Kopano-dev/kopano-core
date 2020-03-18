--TEST--
mapi_inetmapi_imtomapi() tests
--SKIPIF--
<?php if (!extension_loaded("mapi") || !getenv("KOPANO_SOCKET")) print "skip"; ?>
--FILE--
<?php
require_once(__DIR__.'/helpers.php');

$eml = '
Subject: GPL ole 1
From: =?utf-8?Q?Pieter_Post?= <p.post@kopano.com>
To: =?utf-8?Q?Pieter_Post?= <p.post@kopano.com>
Reply-to: =?utf-8?Q?Pieter_Post?= <k.kost@kopano.com>
Date: Wed, 24 Sep 2014 08:34:01 +0000
Mime-Version: 1.0
Content-Type: multipart/alternative;
 boundary="=_f8E8aS4J7YFjiiEG+QoVpc2DgV3NNoLRTHgLyUJ6KEoX38J2"
X-Priority: 3 (Normal)
X-Original-To:

This is a multi-part message in MIME format. Your mail reader does not
understand MIME message format.
--=_f8E8aS4J7YFjiiEG+QoVpc2DgV3NNoLRTHgLyUJ6KEoX38J2
Content-Type: text/plain; charset=utf-8
Content-Transfer-Encoding: quoted-printable

The licenses for most software and other practical works are designed to =
take away your freedom to share and change the works. By contrast, the GN=
U General Public License is intended to guarantee your freedom to share a=
nd change all versions of a program--to make sure it remains free softwar=
e for all its users. We, the Free Software Foundation, use the GNU Genera=
l Public License for most of our software; it applies also to any other w=
ork released this way by its authors. You can apply it to your programs, =
too.=0D=0A=0D=0A
--=_f8E8aS4J7YFjiiEG+QoVpc2DgV3NNoLRTHgLyUJ6KEoX38J2
Content-Type: text/html; charset=utf-8
Content-Transfer-Encoding: quoted-printable

<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN" "http://ww=
w.w3.org/TR/html4/loose.dtd"><html>=0A<head>=0A  <meta name=3D"Generator"=
 content=3D"Zarafa WebApp v7.1.11-46050">=0A  <meta http-equiv=3D"Content=
-Type" content=3D"text/html; charset=3Dutf-8">=0A  <title>GPL</title>=0A<=
/head>=0A<body>=0A<p style=3D"padding: 0; margin: 0;"><span style=3D"font=
-size: 10pt; font-family: tahoma,arial,helvetica,sans-serif;">The license=
s for most software and other practical works are designed to take away y=
our freedom to share and change the works. By contrast, the GNU General P=
ublic License is intended to guarantee your freedom to share and change a=
ll versions of a program--to make sure it remains free software for all i=
ts users. We, the Free Software Foundation, use the GNU General Public Li=
cense for most of our software; it applies also to any other work release=
d this way by its authors. You can apply it to your programs, too.</span>=
</p>=0A</body>=0A</html>
--=_f8E8aS4J7YFjiiEG+QoVpc2DgV3NNoLRTHgLyUJ6KEoX38J2--
';

$session = getMapiSession();
$store = getDefaultStore($session);
$addrbook = mapi_openaddressbook($session);
$root = mapi_msgstore_openentry($store, null);
$message = mapi_folder_createmessage($root);

var_dump(mapi_inetmapi_imtomapi($session, $store, $addrbook, $message, $eml, array()));
--EXPECT--
bool(true)
