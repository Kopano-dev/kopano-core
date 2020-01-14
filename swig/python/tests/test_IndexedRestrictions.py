import os

import pytest

from MAPI import FL_SUBSTRING, FL_IGNORECASE
from MAPI.Struct import (SPropValue, SContentRestriction, SAndRestriction,
                         SOrRestriction, SExistRestriction, SNotRestriction)
from MAPI.Tags import PR_SUBJECT


if not os.getenv('KOPANO_TEST_SERVER'):
    pytest.skip('No kopano-server running', allow_module_level=True)


def assert_no_results(root, restriction):
    table = root.GetContentsTable(0)
    table.Restrict(restriction, 0)
    assert table.QueryRows(-1, 0) == []


def test_optimize_doubleand(root):
    restriction = SAndRestriction([
            SAndRestriction([
                    SContentRestriction(FL_SUBSTRING | FL_IGNORECASE, PR_SUBJECT, SPropValue(PR_SUBJECT, b'unknown')),
                    SContentRestriction(FL_SUBSTRING | FL_IGNORECASE, PR_SUBJECT, SPropValue(PR_SUBJECT, b'unknown'))
                    ])
            ])
    assert_no_results(root, restriction)


def test_optimize_trippleand(root):
    restriction = SAndRestriction([
            SAndRestriction([
                    SAndRestriction([
                            SContentRestriction(FL_SUBSTRING | FL_IGNORECASE, PR_SUBJECT, SPropValue(PR_SUBJECT, b'unknown')),
                            SContentRestriction(FL_SUBSTRING | FL_IGNORECASE, PR_SUBJECT, SPropValue(PR_SUBJECT, b'unknown'))
                            ])
                    ])
            ])
    assert_no_results(root, restriction)


def test_optimize_doubleor(root):
    restriction = SOrRestriction([
            SOrRestriction([
                    SContentRestriction(FL_SUBSTRING | FL_IGNORECASE, PR_SUBJECT, SPropValue(PR_SUBJECT, b'unknown')),
                    SContentRestriction(FL_SUBSTRING | FL_IGNORECASE, PR_SUBJECT, SPropValue(PR_SUBJECT, b'unknown'))
                    ])
            ])
    assert_no_results(root, restriction)


def test_optimize_trippleor(root):
    restriction = SOrRestriction([
            SOrRestriction([
                    SOrRestriction([
                            SContentRestriction(FL_SUBSTRING | FL_IGNORECASE, PR_SUBJECT, SPropValue(PR_SUBJECT, b'unknown')),
                            SContentRestriction(FL_SUBSTRING | FL_IGNORECASE, PR_SUBJECT, SPropValue(PR_SUBJECT, b'unknown'))
                            ])
                    ])
            ])
    assert_no_results(root, restriction)


def test_optimize_false(root):
    restriction = SAndRestriction([
        SNotRestriction(SExistRestriction(PR_SUBJECT)),
        SExistRestriction(PR_SUBJECT)
    ])
    assert_no_results(root, restriction)


def test_optimize_orfalse(root):
    restriction = SOrRestriction([
            SNotRestriction(SExistRestriction(PR_SUBJECT)),
            SExistRestriction(PR_SUBJECT)
    ])
    assert_no_results(root, restriction)


def test_optimize_orandfalse(root):
    restriction = SOrRestriction([
            SAndRestriction([
                    SNotRestriction(SExistRestriction(PR_SUBJECT)),
                    SExistRestriction(PR_SUBJECT)
                    ])
            ])
    assert_no_results(root, restriction)


def test_optimize_ormultianndfalse(root):
    restriction = SOrRestriction([
    SAndRestriction([
            SNotRestriction(SExistRestriction(PR_SUBJECT)),
            SExistRestriction(PR_SUBJECT)
            ]),
    SAndRestriction([
            SContentRestriction(FL_SUBSTRING | FL_IGNORECASE, PR_SUBJECT, SPropValue(PR_SUBJECT, b'unknown')),
            SContentRestriction(FL_SUBSTRING | FL_IGNORECASE, PR_SUBJECT, SPropValue(PR_SUBJECT, b'unknown'))
            ])
    ])
    assert_no_results(root, restriction)
