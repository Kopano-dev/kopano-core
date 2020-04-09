from MAPI import WrapCompressedRTFStream, MAPI_MODIFY, MAPI_CREATE, STGM_WRITE
from MAPI.Struct import SPropValue
from MAPI.Tags import (PR_BODY, PR_BODY_W, PR_HTML, PR_RTF_COMPRESSED,
                       PR_INTERNET_CPID, IID_IStream, IID_IMessage)


def test_plain_rtf_back(message, copy):
    # test for #7067
    message.SetProps([SPropValue(PR_BODY, u"abcé'def".encode('utf8'))])
    message.SaveChanges(0)
    props = message.GetProps([PR_BODY], 0)

    assert props[0].Value == u"abcé'def".encode('utf8')

    stream = message.OpenProperty(PR_HTML, IID_IStream, 0, 0)
    html = stream.Read(10240)
    assert html == b'<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 3.2//EN">\n<HTML>\n<HEAD>\n<META HTTP-EQUIV="Content-Type" CONTENT="text/html; charset=us-ascii">\n<META NAME="Generator" CONTENT="Kopano HTML builder 1.0">\n<TITLE></TITLE>\n</HEAD>\n<BODY>\n<!-- Converted from text/plain format -->\n\n<P><FONT STYLE="font-family: courier" SIZE=2>\nabc&eacute;\'def</FONT>\n</P>\n\n</BODY></HTML>'

    stream = message.OpenProperty(PR_RTF_COMPRESSED, IID_IStream, 0, 0)
    rtfprop = WrapCompressedRTFStream(stream, 0)
    rtf = rtfprop.Read(10240)
    assert rtf == b"{\\rtf1\\ansi\\ansicpg1252\\fromtext \\deff0{\\fonttbl\n{\\f0\\fswiss Arial;}\n{\\f1\\fmodern Courier New;}\n{\\f2\\fnil\\fcharset2 Symbol;}\n{\\f3\\fmodern\\fcharset0 Courier New;}}\n{\\colortbl\\red0\\green0\\blue0;\\red0\\green0\\blue255;}\n\\uc1\\pard\\plain\\deftab360 \\f0\\fs20 abc\\'E9'def}"

    # force generate from RTF to html by excluding PR_HTML
    message.CopyTo([], [PR_HTML], 0, None, IID_IMessage, copy, 0)
    stream = copy.OpenProperty(PR_HTML, IID_IStream, 0, 0)
    html = stream.Read(10240)
    # conversion is quite different (enters \r\n instead of \n, charset not us-ascii anymore)
    # so just find the real corruption error and test that. (faulty code generated 'abc\x9\xdef')
    # depending on the converter and copy source body, either one can be correct, except for the faulty one mentioned.
    assert html.find(b"abc&#233;'def") != -1 or html.find(b"abc&eacute;'def") != -1


def test_HTMLToRTFAndBack(message, copy):
    # test for #7244
    message.SetProps([SPropValue(PR_INTERNET_CPID, 28605), SPropValue(PR_HTML, b'<html><body>I&#8217;d like that.<br></body></html>')])
    plain = message.GetProps([PR_BODY_W], 0)[0].Value
    assert plain == 'I’d like that.\r\n'
    stream = message.OpenProperty(PR_RTF_COMPRESSED, IID_IStream, 0, 0)
    rtfprop = WrapCompressedRTFStream(stream, 0)
    rtf = rtfprop.Read(10240)
    assert rtf == b'{\\rtf1\\ansi\\ansicpg1252\\fromhtml1 \\deff0{\\fonttbl\r\n{\\f0\\fswiss\\fcharset0 Arial;}\r\n{\\f1\\fmodern Courier New;}\r\n{\\f2\\fnil\\fcharset2 Symbol;}\r\n{\\f3\\fmodern\\fcharset0 Courier New;}\r\n{\\f4\\fswiss\\fcharset0 Arial;}\r\n{\\f5\\fswiss Tahoma;}\r\n{\\f6\\fswiss\\fcharset0 Times New Roman;}}\r\n{\\colortbl\\red0\\green0\\blue0;\\red0\\green0\\blue255;\\red0\\green0\\blue255;}\r\n\\uc1\\pard\\plain\\deftab360 \\f0\\fs24 \r\n{\\*\\htmltag19 <html>}{\\*\\htmltag64}\r\n{\\*\\htmltag50 <body>}{\\*\\htmltag64}I\\htmlrtf \\u8217 ?\\htmlrtf0{\\*\\htmltag80&#8217;}d like that.\r\n{\\*\\htmltag112 <br>}{\\*\\htmltag64}\\htmlrtf \\line \\htmlrtf0 \r\n{\\*\\htmltag58 </body>}{\\*\\htmltag64}\r\n{\\*\\htmltag27 </html>}{\\*\\htmltag64}}\r\n'

    message.SaveChanges(0)
    # note: this copies the PR_RTF_COMPRESSED, and re-creates the plain+html bodies in copy. (rtf->html + rtf->plain)
    message.CopyTo([], None, 0, None, IID_IMessage, copy, 0)
    html = copy.GetProps([PR_HTML], 0)[0].Value
    # previously, we had a leading enter here because of rtf conversion (we could strip that in Util::HrHtmlToRtf())
    # but since we now copy from plain again, the enter isn't present anymore
    assert html == b"<html><body>I&#8217;d like that.<br></body></html>"
    plain = copy.GetProps([PR_BODY_W], 0)[0].Value
    assert plain == 'I’d like that.\r\n'


def test_rtfhebrew(message):
    rtf = b"{\\rtf1\\fbidis\\ansi\\ansicpg1252\\deff0\\deflang1033{\\fonttbl{\\f0\\fswiss\\fcharset177 Arial;}{\\f1\\fswiss\\fcharset0 Verdana;}}\n{\\colortbl ;\\red0\\green0\\blue0;}\n{\\*\\generator Riched20 12.0.4518.1014;}\\viewkind4\\uc1\n\\pard\\ltrpar\\cf1\\f0\\rtlch\\fs32\\lang1037\\'f9\\'f4\\'e5\\'f8\\'f1\\'ee\\'e5 \\'e1\\'f2\\'e9\\'fa\\'e5\\'ef \\'e4\\'e0\\'f8\\'f5 \\'e5\\'ee\\'e1\\'e6\\'f7\\'e9\\'ed \\'e4\\'ee\\'f2\\'e5\\'e3\\'eb\\'f0\\'e9\\'ed \\'e1\\'e6\\'ee\\'ef \\'e0\\'ee\\'fa \\'e1\\'f0\\'e5\\'f9\\'e0\\'e9\\'ed \\'ee\\'e3\\'e9\\'f0\\'e9\\par\n\\f1\\ltrch\\lang1033\\par\n}"
    comp = message.OpenProperty(PR_RTF_COMPRESSED, IID_IStream, STGM_WRITE, MAPI_MODIFY | MAPI_CREATE)
    uncomp = WrapCompressedRTFStream(comp, 0)
    uncomp.Write(rtf)
    uncomp.Commit(0)
    comp.Commit(0)
    comp.Seek(0, 0)
    body = message.GetProps([PR_BODY], 0)[0].Value
    assert body == u'שפורסמו בעיתון הארץ ומבזקים המעודכנים בזמן אמת בנושאים מדיני\r\n'.encode('utf8')


# TODO: make C++ based unit tests? See make check
def test_rtfcyrilliceuro(message):
    rtf = b"""
    {\\rtf1\\ansi\\ansicpg1251\\fromhtml1 \\deff0{\\fonttbl
    {\\f0\\fswiss Arial;}
    {\\f1\\fmodern Courier New;}
    {\\f2\\fnil\\fcharset2 Symbol;}
    {\\f3\\fmodern\\fcharset0 Courier New;}
    {\\f4\\fswiss\\fcharset204 Arial;}}
    {\\colortbl\\red0\\green0\\blue0;\\red0\\green0\\blue255;}
    \\uc1\\pard\\plain\\deftab360 \\f0\\fs24
    {\\*\\htmltag19 <html>}
    {\\*\\htmltag2 \\par }
    {\\*\\htmltag2 \\par }
    {\\*\\htmltag34 <head>}
    {\\*\\htmltag1 \\par }
    {\\*\\htmltag1 \\par }
    {\\*\\htmltag161 <meta name=Generator content="Microsoft Word 11 (filtered)">}
    {\\*\\htmltag1 \\par }
    {\\*\\htmltag241 <style>}
    {\\*\\htmltag241 \\par <!--\\par  /* Font Definitions */\\par  @font-face\\par \\tab \\{font-family:SimSun;\\par \\tab panose-1:2 1 6 0 3 1 1 1 1 1;\\}\\par @font-face\\par \\tab \\{font-family:"\\\\@SimSun";\\par \\tab panose-1:2 1 6 0 3 1 1 1 1 1;\\}\\par  /* Style Definitions */\\par  p.MsoNormal, li.MsoNormal, div.MsoNormal\\par \\tab \\{margin:0in;\\par \\tab margin-bottom:.0001pt;\\par \\tab font-size:12.0pt;\\par \\tab font-family:"Times New Roman";\\}\\par a:link, span.MsoHyperlink\\par \\tab \\{color:blue;\\par \\tab text-decoration:underline;\\}\\par a:visited, span.MsoHyperlinkFollowed\\par \\tab \\{color:purple;\\par \\tab text-decoration:underline;\\}\\par span.EmailStyle17\\par \\tab \\{font-family:Arial;\\par \\tab color:windowtext;\\}\\par @page Section1\\par \\tab \\{size:8.5in 11.0in;\\par \\tab margin:1.0in 1.25in 1.0in 1.25in;\\}\\par div.Section1\\par \\tab \\{page:Section1;\\}\\par -->\\par }
    {\\*\\htmltag249 </style>}
    {\\*\\htmltag1 \\par }
    {\\*\\htmltag1 \\par }
    {\\*\\htmltag41 </head>}
    {\\*\\htmltag2 \\par }
    {\\*\\htmltag2 \\par }
    {\\*\\htmltag50 <body lang=EN-US link=blue vlink=purple>}
    {\\*\\htmltag0 \\par }
    {\\*\\htmltag0 \\par }
    {\\*\\htmltag96 <div class=Section1>}\\htmlrtf {\\htmlrtf0
    {\\*\\htmltag0 \\par }
    {\\*\\htmltag0 \\par }
    {\\*\\htmltag64 <p class=MsoNormal>}\\htmlrtf {\\htmlrtf0
    {\\*\\htmltag148 <font size=2 face=Arial>}\\htmlrtf {\\fs20 \\f4 \\htmlrtf0
    {\\*\\htmltag84 <span style='font-size:10.0pt;\\par font-family:Arial'>}\\htmlrtf {\\htmlrtf0 Test \\'88
    {\\*\\htmltag92 </span>}\\htmlrtf }\\htmlrtf0
    {\\*\\htmltag156 </font>}\\htmlrtf }\\htmlrtf0 \\htmlrtf\\par}\\htmlrtf0
    \\htmlrtf \\par
    \\htmlrtf0
    {\\*\\htmltag72 </p>}
    {\\*\\htmltag0 \\par }
    {\\*\\htmltag0 \\par }
    {\\*\\htmltag104 </div>}\\htmlrtf }\\htmlrtf0
    {\\*\\htmltag0 \\par }
    {\\*\\htmltag0 \\par }
    {\\*\\htmltag58 </body>}
    {\\*\\htmltag2 \\par }
    {\\*\\htmltag2 \\par }
    {\\*\\htmltag27 </html>}
    {\\*\\htmltag3 \\par }}
    """

    comp = message.OpenProperty(PR_RTF_COMPRESSED, IID_IStream, STGM_WRITE, MAPI_MODIFY | MAPI_CREATE)
    uncomp = WrapCompressedRTFStream(comp, 0)
    uncomp.Write(rtf)
    uncomp.Commit(0)
    comp.Commit(0)
    comp.Seek(0, 0)
    body = message.GetProps([PR_BODY], 0)[0].Value
    assert body == 'Test €\r\n'.encode('utf8')


def test_RTF_smarttags1(message):
    rtf = b"""
    {\\*\\xmlopen\\xmlns0{\\factoidname PersonName}}Name{\\*\\xmlclose}
    """
    comp = message.OpenProperty(PR_RTF_COMPRESSED, IID_IStream, STGM_WRITE, MAPI_MODIFY | MAPI_CREATE)
    uncomp = WrapCompressedRTFStream(comp, 0)
    uncomp.Write(rtf)
    uncomp.Commit(0)
    comp.Commit(0)
    comp.Seek(0, 0)
    body = message.GetProps([PR_BODY], 0)[0].Value
    assert body == b'Name\r\n'


def test_RTF_smarttags2(message):
    # Example from http://support.microsoft.com/kb/922681
    rtf = b"""
    {\\*\\xmlopen\\xmlns2{\\factoidname date}
    {\\xmlattr\\xmlattrns0{\\xmlattrname Month}{\\xmlattrvalue 4}}
    {\\xmlattr\\xmlattrns0{\\xmlattrname Day}{\\xmlattrvalue 11}}
    {\\xmlattr\\xmlattrns0{\\xmlattrname Year}{\\xmlattrvalue 2006}}}4/11/2006}
    {\\*\\xmlclose}
    """
    comp = message.OpenProperty(PR_RTF_COMPRESSED, IID_IStream, STGM_WRITE, MAPI_MODIFY | MAPI_CREATE)
    uncomp = WrapCompressedRTFStream(comp, 0)
    uncomp.Write(rtf)
    uncomp.Commit(0)
    comp.Commit(0)
    comp.Seek(0, 0)
    body = message.GetProps([PR_BODY], 0)[0].Value
    assert body == b'4/11/2006\r\n'


def test_RTF_generator_nosemicolon(message):
    rtf = b"""
    {\\rtf1\\ansi\\ansicpg1252\\deff0\\nouicompat\\deflang1033{\\fonttbl{\\f0\\fswiss\\fcharset0 Calibri;}}
    {\\*\\generator Riched20 15.0.4737}
    \\pard\\cf1\\f0\\fs22 1\\par 2\\par 2\\par 3\\par 4\\par 5\\par}
    """
    comp = message.OpenProperty(PR_RTF_COMPRESSED, IID_IStream, STGM_WRITE, MAPI_MODIFY | MAPI_CREATE)
    uncomp = WrapCompressedRTFStream(comp, 0)
    uncomp.Write(rtf)
    uncomp.Commit(0)
    comp.Commit(0)
    comp.Seek(0, 0)
    body = message.GetProps([PR_BODY], 0)[0].Value
    assert b''.join(body.splitlines()).strip() == b'122345'
    html = message.GetProps([PR_HTML], 0)[0].Value
    assert b'Riched20' not in html
    assert b'1</p>' in html
    assert b'<p>2</p>' in html
    assert b'<p>3</p>' in html
    assert b'<p>4</p>' in html
    assert b'<p>5' in html
