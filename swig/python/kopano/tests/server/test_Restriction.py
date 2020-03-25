from MAPI.Tags import PR_SUBJECT
from MAPI.Struct import SPropertyRestriction, SPropValue
from MAPI import RELOP_EQ

from kopano.restriction import Restriction


def test_subject(item):
    item.subject = 'test'
    mapiobj = SPropertyRestriction(RELOP_EQ, PR_SUBJECT,
                                   SPropValue(PR_SUBJECT, b'test'))
    restriction = Restriction(mapiobj=mapiobj)

    assert restriction.match(item)

    item.subject = 'henk'
    assert not restriction.match(item)


def test_str():
    assert str(Restriction()) == 'Restriction()'
