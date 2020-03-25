from datetime import datetime

import pytest

from kopano import Address, Restriction, Table
from kopano.errors import NotFoundError

from MAPI import RELOP_EQ
from MAPI.Util import SPropValue, SPropertyRestriction
from MAPI.Tags import PR_SUBJECT_W


UNICODE_TEXT = u'\u5927\u53cb \u514b\u6d0b'
# count, users, names, emails
TOCCBCC = [(1, 'pieter post <p.post@kopano.com>', ['pieter post'], ['p.post@kopano.com']),
           (2, 'p.post@kopano.com; b.visser@kopano.com', ['p.post@kopano.com', 'b.visser@kopano.com'], ['p.post@kopano.com', 'b.visser@kopano.com']),
           (2, 'pieter <p.post@kopano.com>;bert <b.visser@kopano.com>', ['pieter', 'bert'], ['p.post@kopano.com', 'b.visser@kopano.com']),
           (1, 'p.post@kopano.com', ['p.post@kopano.com'], ['p.post@kopano.com']),
          ]


def test_sender(email):
    assert email.sender.name == 'Pieter Post'
    assert email.sender.email == 'p.post@kopano.com'


def test_recipeint(email):
    recipient = next(email.recipients())
    assert recipient.name == 'Pieter Post'
    assert recipient.email == 'p.post@kopano.com'


def test_subject(inbox, item, email):
    assert not item.subject

    assert email.subject == 'GPL ole 1'

    email.subject = 'new'
    assert email.subject == 'new'

    email.subject = UNICODE_TEXT
    assert email.subject == UNICODE_TEXT



@pytest.mark.parametrize('count,users,names,emails', TOCCBCC)
def test_to(item, count, users, names, emails):
    item.to = users
    recips = list(item.to)
    assert len(recips) == count
    assert set(r.name for r in recips) == set(names)
    assert set(r.email for r in recips) == set(emails)


@pytest.mark.parametrize('count,users,names,emails', TOCCBCC)
def test_cc(item, count, users, names, emails):
    item.to = users
    recips = list(item.to)
    assert len(recips) == count
    assert set(r.name for r in recips) == set(names)
    assert set(r.email for r in recips) == set(emails)


@pytest.mark.parametrize('count,users,names,emails', TOCCBCC)
def test_bcc(item, count, users, names, emails):
    item.to = users
    recips = list(item.to)
    assert len(recips) == count
    assert set(r.name for r in recips) == set(names)
    assert set(r.email for r in recips) == set(emails)


def test_to_user(item, item_user_toccbcc):
    for count, users, names, emails in item_user_toccbcc:
        item.to = users
        recips = list(item.to)
        assert len(recips) == count
        assert set(r.name for r in recips) == set(names)
        assert set(r.email for r in recips) == set(emails)


def test_cc_user(item, item_user_toccbcc):
    for count, users, names, emails in item_user_toccbcc:
        item.cc = users
        recips = list(item.cc)
        assert len(recips) == count
        assert set(r.name for r in recips) == set(names)
        assert set(r.email for r in recips) == set(emails)


def test_bcc_user(item, item_user_toccbcc):
    for count, users, names, emails in item_user_toccbcc:
        item.bcc = users
        recips = list(item.bcc)
        assert len(recips) == count
        assert set(r.name for r in recips) == set(names)
        assert set(r.email for r in recips) == set(emails)


def test_normalized_subject(item):
    assert not item.normalized_subject

    item.subject = 'RE: RE: birthday'
    item.normalized_subject == 'RE: birthday'

    item.subject = 'FWD: RE: birthday'
    item.normalized_subject == 'RE: birthday'

    item.subject = 'RE: FWD: birthday'
    item.normalized_subject == 'FWD: birthday'


def test_hierarchyid(item):
    assert isinstance(item.hierarchyid, int)


def test_read(email):
    assert not email.read


def test_messageclass(email):
    assert email.message_class == 'IPM.Note'

    email.message_class = 'IPM.Note.SMIME'
    assert email.message_class == 'IPM.Note.SMIME'


def test_stubbed(item):
    assert not item.stubbed


def test_sourckey(item):
    assert len(item.sourcekey) == 44


def test_searchkey(item):
    assert len(item.searchkey) == 32


def test_header(email):
    assert email.header('subject') == 'GPL ole 1'


def test_headers(item, email):
    headers = email.headers()
    assert headers.get('subject') == 'GPL ole 1'

    headers = item.headers()
    assert not item.headers()


def test_eml(email):
    assert email.eml()


def test_delete_prop(item):
    item.subject = 'test'
    item.delete(item.prop(PR_SUBJECT_W))


def test_urgency(email):
    assert email.urgency == 'normal'
    email.urgency = 'high'
    assert email.urgency == 'high'


def test_created(email):
    assert isinstance(email.created, datetime)


def test_lastmodified(email):
    assert isinstance(email.last_modified, datetime)
    assert email.last_modified.date() == datetime.now().date()


def test_received(email):
    assert isinstance(email.received, datetime)

    dt = datetime(2017, 1, 1, 9, 30)
    email.received = dt
    assert email.received == dt


def test_export_vcf(vcfcontact):
    vcf = vcfcontact.vcf()
    assert b"N:post;pieter" in vcf
    assert b"NICKNAME:harry" in vcf
    assert b"forrestgump@example.com" in vcf


def test_categories(item):
    categories = item.categories
    assert not categories

    categories.append('birthday')
    categories.append('important')

    assert set(categories) == set(['birthday', 'important'])

    item.categories = ['pizza']
    assert item.categories == ['pizza']


def test_private(item):
    assert not item.private
    item.private = True
    assert item.private


def test_equality(item, email):
    assert item != email
    assert item == item
    assert email == email


def test_prop(item):
    item.subject = 'test'
    assert item.prop(PR_SUBJECT_W).value == 'test'
    assert item.prop('PR_SUBJECT_W').value == 'test'

    with pytest.raises(NotFoundError) as excinfo:
        item.prop(0x0)
    assert 'no such property' in str(excinfo)


def test_getprop(item):
    item.subject = 'test'
    assert item.get_prop(PR_SUBJECT_W).value == 'test'
    assert not item.get_prop(0x0)


def test_props(item):
    item.subject = 'test'
    assert [prop for prop in item.props() if prop.proptag == PR_SUBJECT_W and prop.value == 'test']


def test_iteration(item):
    item.subject = 'test'
    assert [prop for prop in item if prop.proptag == PR_SUBJECT_W and prop.value == 'test']


def test_get(item):
    item.subject = 'test'
    assert item.get(PR_SUBJECT_W) == 'test'
    assert item.get(0x0) is None
    assert item.get(0x0, 'test') == 'test'


def test_getitem(item):
    item.subject = 'test'
    assert item[PR_SUBJECT_W] == 'test'
    with pytest.raises(NotFoundError) as excinfo:
        item[0x0]
    assert 'no such property' in str(excinfo)


def test_delitem(item):
    item.subject = 'test'
    del item[PR_SUBJECT_W]
    assert not item.subject


def test_setitem(item):
    item[PR_SUBJECT_W] = 'dreams'
    item.subject == 'dreams'


def test_tables(email):
    tables = list(email.tables())
    assert len(tables) == 2
    assert isinstance(tables[0], Table)


def test_rtf(item, email):
    assert email.rtf.startswith(b'{')
    assert b'GNU General Public License' in email.rtf

    assert not item.rtf


def test_replyto(email):
    replyto = list(email.replyto)
    assert len(replyto) == 1
    assert isinstance(replyto[0], Address)
    assert replyto[0].email == 'k.kost@kopano.com'


def test_read_receipt(email):
    assert not email.read_receipt
    email.read_receipt = True
    assert email.read_receipt


def test_delivery_receipt(email):
    assert not email.delivery_receipt


def test_is_meetingrequest(email):
    assert not email.is_meetingrequest


def test_codepage(email):
    assert email.codepage == 65001


def test_encoding(email):
    assert email.encoding == 'utf-8'
    email.codepage = 333
    with pytest.raises(NotFoundError) as excinfo:
        email.encoding
    assert 'no encoding' in str(excinfo)


def test_math(item):
    item.subject = 'test'

    restriction = Restriction(SPropertyRestriction(RELOP_EQ, PR_SUBJECT_W, SPropValue(PR_SUBJECT_W, 'test')))
    assert item.match(restriction)

    restriction = Restriction(SPropertyRestriction(RELOP_EQ, PR_SUBJECT_W, SPropValue(PR_SUBJECT_W, 'boo')))
    assert not item.match(restriction)


def test_str(item):
    assert str(item) == 'Item()'
