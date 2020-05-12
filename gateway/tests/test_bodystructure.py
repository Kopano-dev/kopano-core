alt_text_only = b"""Date: Tue, 14 Dec 2010 11:30:36 +0100
Subject: multipart text only
From: Internet user <internet@localhost>
To: Test user <user1@localhost>
Content-Type: multipart/alternative; BoUnDaRy=unique1
Mime-Version: 1.0

Mime string here
--unique1
Content-Type: text/plain; charset=iso-8859-15
Content-Transfer-Encoding: quoted-printable

Hello,=0A=0AJust one line here.

--unique1
Content-Type: text/html; charset=iso-8859-15
Content-Transfer-Encoding: 8-bit

<html>
html text hier
</html>
--unique1--
"""

quoted_boundary_with_space = b"""Date: Tue, 14 Dec 2010 11:30:36 +0100
Subject: multipart text only
From: Internet user <internet@localhost>
To: Test user <user1@localhost>
Content-Type: multipart/alternative; boundary=\"space in boundary\"
Mime-Version: 1.0

Mime string here
--space in boundary
Content-Type: text/plain; charset=iso-8859-15
Content-Transfer-Encoding: quoted-printable

Hello,=0A=0AJust one line here.

--space in boundary
Content-Type: text/html; charset=iso-8859-15
Content-Transfer-Encoding: 8-bit

<html>
html text hier
</html>
--space in boundary--
"""

related_in_alt = b"""Date: Tue, 14 Dec 2010 11:30:36 +0100
Subject: multipart text only
From: Internet user <internet@localhost>
To: Test user <user1@localhost>
Content-Type: multipart/alternative; boundary=unique1
Mime-Version: 1.0

Mime string here
--unique1
Content-Type: text/plain; charset=iso-8859-15
Content-Transfer-Encoding: quoted-printable

Hello,=0A=0AJust one line here.

--unique1
Content-Type: multipart/related; boundary=unique2

--unique2
Content-Type: text/html; charset=iso-8859-15
Content-Transfer-Encoding: 8-bit

<html>
<img src=\"cid:12345@inline\">
</html>
--unique2
Content-Type: image/gif
Content-Transfer-Encoding: base64
Content-Disposition: inline; filename=small.gif
Content-Id: <12345@inline>

R0lGODdhIAAgAMZHAC5B1y9C1zBC1zBD1zFE2DJF2DRG2DVH2DVI2DZI2TdJ2TlL2TpM2TtN2jxO
2j1O2j5P2j5Q2j9R2kJT20JU20RV20dX3EdY3EhZ3Ela3Etb3E1d3U5e3U9f3VBg3lJi3lVl31Zm
31hn31xr4F9u4GJx4WNx4WRy4Wd04md14ml34nB95HJ/5HWC5XeD5XiE5YOO54eS6ImU6IqU6YuV
6ZCa6peg66Cp7aGp7aSs7qqy77e98b3D8svP9c3S9c7S9tXZ99nd+OXn+uvt+/Lz/PT1/fX2/f//
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////ywA
AAAAIAAgAAAH/oBHgoOEhYaHiImKi4yNjo+LRD05OT1EkIo7GgEBFDY4KR8ZExATGRuoqaijpRsp
OIMvAQC0s7S3uLm6AAGDKrvAwbiDNbbCx8OCQSDIzbSEQjALtwgYrKapGQvGuoVFPTYxOkKHQjUR
wQEdmEIl3LgGFi0/jEKgogrADTXkjUAiA9JRkNHP3wVhEXqwExGMwAp6mGa8u4VBIaYjQhrgEkDh
w6uCkP7lUjHkIiEgB3ENsGjyyA8MuQKwMNLSJYVcAlaAZNch1wAXOwUBWeEgwQIHE1zhACkRVwAX
JQUZ6UFjxIUC+mwMypiLgIVUGh4ERIbhSI8QzpyVLZG2WQOtMG1jVjihQULSj0fYph3wgAOKG0EH
9WCgS4CDU6g8kKDRo4gjHyUOACCwwAQPxzUDAQA7
--unique2--

--unique1--
"""

related_with_prefix_boundary = b"""Date: Tue, 14 Dec 2010 11:30:36 +0100
Subject: multipart text only
From: Internet user <internet@localhost>
To: Test user <user1@localhost>
Content-Type: multipart/alternative; boundary=unique
Mime-Version: 1.0

Mime string here
--unique
Content-Type: text/plain; charset=iso-8859-15
Content-Transfer-Encoding: quoted-printable

Hello,=0A=0AJust one line here.

--unique
Content-Type: multipart/related; boundary=unique2

--unique2
Content-Type: text/html; charset=iso-8859-15
Content-Transfer-Encoding: 8-bit

<html>
<img src=\"cid:12345@inline\">
</html>
--unique2
Content-Type: image/gif
Content-Transfer-Encoding: base64
Content-Disposition: inline; filename=small.gif
Content-Id: <12345@inline>

R0lGODdhIAAgAMZHAC5B1y9C1zBC1zBD1zFE2DJF2DRG2DVH2DVI2DZI2TdJ2TlL2TpM2TtN2jxO
2j1O2j5P2j5Q2j9R2kJT20JU20RV20dX3EdY3EhZ3Ela3Etb3E1d3U5e3U9f3VBg3lJi3lVl31Zm
31hn31xr4F9u4GJx4WNx4WRy4Wd04md14ml34nB95HJ/5HWC5XeD5XiE5YOO54eS6ImU6IqU6YuV
6ZCa6peg66Cp7aGp7aSs7qqy77e98b3D8svP9c3S9c7S9tXZ99nd+OXn+uvt+/Lz/PT1/fX2/f//
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////ywA
AAAAIAAgAAAH/oBHgoOEhYaHiImKi4yNjo+LRD05OT1EkIo7GgEBFDY4KR8ZExATGRuoqaijpRsp
OIMvAQC0s7S3uLm6AAGDKrvAwbiDNbbCx8OCQSDIzbSEQjALtwgYrKapGQvGuoVFPTYxOkKHQjUR
wQEdmEIl3LgGFi0/jEKgogrADTXkjUAiA9JRkNHP3wVhEXqwExGMwAp6mGa8u4VBIaYjQhrgEkDh
w6uCkP7lUjHkIiEgB3ENsGjyyA8MuQKwMNLSJYVcAlaAZNch1wAXOwUBWeEgwQIHE1zhACkRVwAX
JQUZ6UFjxIUC+mwMypiLgIVUGh4ERIbhSI8QzpyVLZG2WQOtMG1jVjihQULSj0fYph3wgAOKG0EH
9WCgS4CDU6g8kKDRo4gjHyUOACCwwAQPxzUDAQA7
--unique2--

--unique--
"""

boundary_in_body = b"""Date: Tue, 14 Dec 2010 11:30:36 +0100
Subject: multipart text only
From: Internet user <internet@localhost>
To: Test user <user1@localhost>
Content-Type: multipart/alternative; boundary=unique1
Mime-Version: 1.0

Mime string here
--unique1
Content-Type: text/plain; charset=iso-8859-15
Content-Transfer-Encoding: quoted-printable

I sense the boundary will be --unique1
and this will matter.

--unique1
Content-Type: text/html; charset=iso-8859-15
Content-Transfer-Encoding: 8-bit

<html>
html text hier
</html>
--unique1--
"""



def test_alternative(imap, folder):
    assert imap.append(folder, None, None, alt_text_only)[0] == 'OK'
    assert imap.select(folder)[0] == 'OK'

    result = imap.fetch('1', '(bodystructure)')
    # courier has one less NIL in the extended data, and thus fails here.
    assert result[1][0] == b'1 (BODYSTRUCTURE (("text" "plain" ("charset" "iso-8859-15") NIL NIL "quoted-printable" 33 1 NIL NIL NIL NIL)("text" "html" ("charset" "iso-8859-15") NIL NIL "8-bit" 31 2 NIL NIL NIL NIL) "alternative" ("BoUnDaRy" "unique1") NIL NIL NIL))'

    # use peek not to make message read
    result = imap.fetch('1', '(body.peek[1])')
    assert 33 == len(result[1][0][1])

    result = imap.fetch('1', '(body.peek[2])')
    assert 31 == len(result[1][0][1])


def test_spaceboundary(imap, folder):
    assert imap.append(folder, None, None, quoted_boundary_with_space)[0] == 'OK'
    assert imap.select(folder)[0] == 'OK'

    result = imap.fetch('1', '(bodystructure)')
    # courier has one less NIL in the extended data, and thus fails here.
    assert result[1][0] == b'1 (BODYSTRUCTURE (("text" "plain" ("charset" "iso-8859-15") NIL NIL "quoted-printable" 33 1 NIL NIL NIL NIL)("text" "html" ("charset" "iso-8859-15") NIL NIL "8-bit" 31 2 NIL NIL NIL NIL) "alternative" ("boundary" "space in boundary") NIL NIL NIL))'

    # use peek not to make message read
    result = imap.fetch('1', '(body.peek[1])')
    assert 33 == len(result[1][0][1])

    result = imap.fetch('1', '(body.peek[2])')
    assert 31 == len(result[1][0][1])


def test_relatedinalt(imap, folder):
    assert imap.append(folder, None, None, related_in_alt)[0] == 'OK'
    assert imap.select(folder)[0] == 'OK'

    result = imap.fetch('1', '(bodystructure)')
    # courier has one less NIL in the extended data, and thus fails here.
    assert result[1][0] == b'1 (BODYSTRUCTURE (("text" "plain" ("charset" "iso-8859-15") NIL NIL "quoted-printable" 33 1 NIL NIL NIL NIL)(("text" "html" ("charset" "iso-8859-15") NIL NIL "8-bit" 45 2 NIL NIL NIL NIL)("image" "gif" NIL "<12345@inline>" NIL "base64" 976 NIL ("inline" ("filename" "small.gif")) NIL NIL) "related" ("boundary" "unique2") NIL NIL NIL) "alternative" ("boundary" "unique1") NIL NIL NIL))'

    # use peek not to make message read
    result = imap.fetch('1', '(body.peek[1])')
    assert 33 == len(result[1][0][1])

    result = imap.fetch('1', '(body.peek[2])')
    assert 1281 == len(result[1][0][1])

    result = imap.fetch('1', '(body.peek[2.1])')
    assert 45 == len(result[1][0][1])

    result = imap.fetch('1', '(body.peek[2.2])')
    assert 976 == len(result[1][0][1])


def test_related_prefix_boundary(imap, folder):
    assert imap.append(folder, None, None, related_with_prefix_boundary)[0] == 'OK'
    assert imap.select(folder)[0] == 'OK'

    result = imap.fetch('1', '(bodystructure)')
    # courier has one less NIL in the extended data, and thus fails here.
    assert result[1][0] == b'1 (BODYSTRUCTURE (("text" "plain" ("charset" "iso-8859-15") NIL NIL "quoted-printable" 33 1 NIL NIL NIL NIL)(("text" "html" ("charset" "iso-8859-15") NIL NIL "8-bit" 45 2 NIL NIL NIL NIL)("image" "gif" NIL "<12345@inline>" NIL "base64" 976 NIL ("inline" ("filename" "small.gif")) NIL NIL) "related" ("boundary" "unique2") NIL NIL NIL) "alternative" ("boundary" "unique") NIL NIL NIL))'

    # use peek not to make message read
    result = imap.fetch('1', '(body.peek[1])')
    assert 33 == len(result[1][0][1])

    result = imap.fetch('1', '(body.peek[2])')
    assert 1281 == len(result[1][0][1])

    result = imap.fetch('1', '(body.peek[2.1])')
    assert 45 == len(result[1][0][1])

    result = imap.fetch('1', '(body.peek[2.2])')
    assert 976 == len(result[1][0][1])


def test_boundary_in_body(imap, folder):
    assert imap.append(folder, None, None, boundary_in_body)[0] == 'OK'
    assert imap.select(folder)[0] == 'OK'

    result = imap.fetch('1', '(bodystructure)')

    # courier has one less NIL in the extended data, and thus fails here.
    assert result[1][0] == b'1 (BODYSTRUCTURE (("text" "plain" ("charset" "iso-8859-15") NIL NIL "quoted-printable" 63 2 NIL NIL NIL NIL)("text" "html" ("charset" "iso-8859-15") NIL NIL "8-bit" 31 2 NIL NIL NIL NIL) "alternative" ("boundary" "unique1") NIL NIL NIL))'

    # use peek not to make message read
    result = imap.fetch('1', '(body.peek[1])')
    assert 63 == len(result[1][0][1])

    result = imap.fetch('1', '(body.peek[2])')
    assert 31 == len(result[1][0][1])
