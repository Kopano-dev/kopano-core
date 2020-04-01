import os

from MAPI import RELOP_EQ
from MAPI.Util import SPropValue, SPropertyRestriction
from MAPI.Tags import (PR_ACCOUNT, PR_DISPLAY_NAME, PR_DISPLAY_NAME_W,
                       MAPI_UNRESOLVED, MAPI_RESOLVED, MAPI_AMBIGUOUS,
                       PR_ANR_W, PR_ANR_A,
                       IID_IUnknown, IID_IMAPIProp, IID_IMAPIContainer,
                       IID_IABContainer)


def test_resolvenames(gab):
    account = os.getenv('KOPANO_TEST_USER3').encode()
    (rows, flags) = gab.ResolveNames([PR_ACCOUNT], 0, [[SPropValue(PR_DISPLAY_NAME, account)]], [MAPI_UNRESOLVED])
    assert flags == [MAPI_RESOLVED]
    assert rows == [[SPropValue(PR_DISPLAY_NAME, account), SPropValue(PR_ACCOUNT, account)]]


def test_ambiguous(gab):
    (rows, flags) = gab.ResolveNames([PR_ACCOUNT], 0, [[SPropValue(PR_DISPLAY_NAME, b'user')]], [MAPI_UNRESOLVED])
    assert flags == [MAPI_AMBIGUOUS]
    assert rows == [[SPropValue(PR_DISPLAY_NAME, b'user')]]


def test_restrict_anr(gab):
    account = os.getenv('KOPANO_TEST_USER3')
    fullname = os.getenv('KOPANO_TEST_FULLNAME3')
    contents = gab.GetContentsTable(0)
    contents.SetColumns([PR_DISPLAY_NAME], 0)
    contents.Restrict(SPropertyRestriction(RELOP_EQ, PR_ANR_W, SPropValue(PR_ANR_W, account)), 0)
    rows = contents.QueryRows(-1, 0)
    assert len(rows) == 1
    assert rows[0] == [SPropValue(PR_DISPLAY_NAME, fullname.encode())]


def test_restrict_anr_ascii(gab):
    account = os.getenv('KOPANO_TEST_USER3').encode()
    fullname = os.getenv('KOPANO_TEST_FULLNAME3')
    contents = gab.GetContentsTable(0)
    contents.SetColumns([PR_DISPLAY_NAME_W], 0)
    contents.Restrict(SPropertyRestriction(RELOP_EQ, PR_ANR_A, SPropValue(PR_ANR_A, account)), 0)
    rows = contents.QueryRows(-1, 0)
    assert len(rows) == 1
    assert rows[0] == [SPropValue(PR_DISPLAY_NAME_W, fullname)]


def test_restrict_anr_partialcase(gab):
    contents = gab.GetContentsTable(0)
    contents.SetColumns([PR_DISPLAY_NAME], 0)
    contents.Restrict(SPropertyRestriction(RELOP_EQ, PR_ANR_A, SPropValue(PR_ANR_W, 'EVERY')), 0)
    rows = contents.QueryRows(-1, 0)
    assert len(rows) == 1
    assert rows[0] == [SPropValue(PR_DISPLAY_NAME, b'Everyone')]


def test_restrict_anr_partialcase_nonprefix(gab):
    contents = gab.GetContentsTable(0)
    contents.SetColumns([PR_DISPLAY_NAME], 0)
    contents.Restrict(SPropertyRestriction(RELOP_EQ, PR_ANR_A, SPropValue(PR_ANR_W, 'VERY')), 0)
    rows = contents.QueryRows(-1, 0)
    assert not rows


def test_interfaces(gab):
    assert gab.QueryInterface(IID_IUnknown)
    assert gab.QueryInterface(IID_IMAPIProp)
    assert gab.QueryInterface(IID_IMAPIContainer)
    assert gab.QueryInterface(IID_IABContainer)
