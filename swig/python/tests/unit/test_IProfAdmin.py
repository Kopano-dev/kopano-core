import pytest

from MAPI.Struct import SPropValue, MAPIError
from MAPI import BOOKMARK_BEGINNING


def test_createprof_nulluserpass(adminprof):
    with pytest.raises(MAPIError) as excinfo:
        adminprof.CreateProfile(None, None, 0, 0)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo.value)


def test_createprof_nulluser(adminprof):
    with pytest.raises(MAPIError) as excinfo:
        adminprof.CreateProfile(None, b'pass', 0, 0)
    assert 'MAPI_E_INVALID_PARAMETER' in str(excinfo.value)


def test_create_prof(adminprof):
    assert adminprof.CreateProfile(b'Prof', None, 0, 0) is None
    assert adminprof.CreateProfile(b'Prof2', None, 0, 0) is None

    assert adminprof.DeleteProfile(b'Prof', 0) is None
    assert adminprof.DeleteProfile(b'Prof2', 0) is None


def test_create_prof_case(adminprof):
    assert adminprof.CreateProfile(b'A', None, 0, 0) is None
    assert adminprof.CreateProfile(b'a', None, 0, 0) is None

    assert adminprof.DeleteProfile(b'A', 0) is None
    assert adminprof.DeleteProfile(b'a', 0) is None


def test_create_prof_double(adminprof):
    adminprof.CreateProfile(b'A', None, 0, 0)

    with pytest.raises(MAPIError) as excinfo:
        adminprof.CreateProfile(b'A', None, 0, 0)
    assert 'MAPI_E_NO_ACCESS' in str(excinfo.value)

    adminprof.DeleteProfile(b'A', 0)


def test_create_prof_nonexsists(adminprof):
    with pytest.raises(MAPIError) as excinfo:
        adminprof.DeleteProfile(b'A', 0)
    assert 'MAPI_E_NOT_FOUND' in str(excinfo.value)


def test_adminservices(adminprof):
    adminprof.CreateProfile(b'Admin', None, 0, 0)
    assert adminprof.AdminServices(b'Admin', None, 0, 0) is not None
    adminprof.DeleteProfile(b'Admin', 0)


def test_profiletable_rows(adminprof):
    adminprof.CreateProfile(b'a1', None, 0, 0)
    adminprof.CreateProfile(b'a2', None, 0, 0)

    table = adminprof.GetProfileTable(0)
    table.SeekRow(BOOKMARK_BEGINNING, 0)

    rows = table.QueryRows(1, 0)
    assert len(rows) == 1

    rows = table.QueryRows(1, 0)
    assert len(rows) == 1
    rows = table.QueryRows(1, 0)
    assert len(rows) == 0

    adminprof.DeleteProfile(b'a1', 0)
    adminprof.DeleteProfile(b'a2', 0)


def test_profiletable_rowcontent(adminprof):
    adminprof.CreateProfile(b'a1', None, 0, 0)
    adminprof.CreateProfile(b'a2', None, 0, 0)

    table = adminprof.GetProfileTable(0)
    table.SeekRow(BOOKMARK_BEGINNING, 0)

    rows = table.QueryRows(1, 0)
    assert len(rows) == 1
    assert rows == [[SPropValue(0x3D04000B, False), SPropValue(0x3001001E, b'a1')]]

    rows = table.QueryRows(1, 0)
    assert len(rows) == 1

    adminprof.DeleteProfile(b'a1', 0)
    adminprof.DeleteProfile(b'a2', 0)
