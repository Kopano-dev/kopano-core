from datetime import datetime

import pytest

from kopano.pidlid import PidLidLocation
from kopano.errors import Error

from MAPI.Time import unixtime
from MAPI.Util import SPropValue
from MAPI.Tags import (PR_ENTRYID, PR_SUBJECT, PR_SUBJECT_W, PR_BODY_W,
                       PR_MESSAGE_DELIVERY_TIME, PT_UNICODE, PT_SYSTIME,
                       PT_BOOLEAN)


def test_body(email):
    # streaming
    prop1 = [p for p in email.props() if p.proptag == PR_BODY_W][0]
    assert prop1.id_ == 0x1000
    assert prop1.type_ == PT_UNICODE
    assert prop1.proptag == PR_BODY_W

    prop2 = email.prop(PR_BODY_W)
    for value in (prop1.value, prop2.value, email.text):
        assert isinstance(value, str)

    # non-streaming
    prop1 = [p for p in email.props() if p.proptag == PR_BODY_W][0]
    assert prop1.id_ == 0x1000
    assert prop1.type_ == PT_UNICODE
    assert prop1.proptag == PR_BODY_W
    prop2 = email.prop(PR_BODY_W)
    for value in (prop1.value, prop2.value):
        assert isinstance(value, str)


def test_subject(email):
    prop1 = [p for p in email.props() if p.proptag == PR_SUBJECT_W][0]

    assert prop1.id_ == 0x0037
    assert prop1.type_ == PT_UNICODE
    assert prop1.proptag == PR_SUBJECT_W

    prop2 = email.prop(PR_SUBJECT_W)
    for value in (prop1.value, prop2.value, email.subject):
        assert isinstance(value, str)
        assert value == email.subject


def testReceived(email):
    value1 = [p for p in email.props() if p.proptag == PR_MESSAGE_DELIVERY_TIME][0].value
    value2 = email.prop(PR_MESSAGE_DELIVERY_TIME).value
    value3 = email.received
    for value in (value1, value2, value3):
        assert value == email.received


def test_attributes(email):
    assert email.prop(PR_SUBJECT).strid == 'PR_SUBJECT'
    # TODO: only works with dagent?
    #assert email.prop('internet_headers:x-mailer').strid == 'internet_headers:x-mailer'
    assert email.prop(PR_SUBJECT_W).strval == email.subject
    assert email.prop(PR_ENTRYID).strval, email.entryid


def test_namespace(email):
    ''' Test namespace for item.prop('namespace:property') '''
    return

    # Different code path then, item.prop()
    # TODO: only works with dagent?
    #assert not email.prop('internet_headers:x-mailer').value


def test_setter(email):
    subject = email.prop(PR_SUBJECT_W)
    subject.value = u'new'
    assert subject.value == u'new'


def test_createproperty(item):
    # Normal property
    item.create_prop(PR_SUBJECT_W, 'subject')
    assert item.subject == 'subject'

    # SYSTIME
    now = datetime.now().replace(second=0, microsecond=0)
    item.create_prop(PR_MESSAGE_DELIVERY_TIME, now)
    assert item.prop(PR_MESSAGE_DELIVERY_TIME).value == now

    # named and SYSTIME
    item.create_prop('common:34070', now, proptype=PT_SYSTIME)
    assert item.prop('common:34070').value == now

    # named normal
    item.create_prop('appointment:33315', True, proptype=PT_BOOLEAN)
    assert item.prop('appointment:33315').value

    # Exception cases, no proptype
    with pytest.raises(Error) as excinfo:
        item.create_prop('appointment:33315', True)
    assert 'Missing type to create' in str(excinfo)

    # Exception case, type and value do not match
    with pytest.raises(Error) as excinfo:
        item.create_prop('appointment:33315', True, PT_UNICODE)
    assert 'Could not create' in str(excinfo)


def test_Y10K(item):
    # datetime can't handle 4+ digit years, so we cut them off at 9999-1-1
    value = unixtime(10000*365*24*3600)
    item.mapiobj.SetProps([SPropValue(PR_MESSAGE_DELIVERY_TIME, value)])
    item.mapiobj.SaveChanges(0)
    assert item.prop(PR_MESSAGE_DELIVERY_TIME).value == datetime(9999, 1, 1)


def testStreaming(server, user, inbox, item):
    # create test item (with body prop, normal prop, named prop)
    HTML = b'baka'*40000
    BODY = 'baka'*40000
    SUBJECT = 'hoppa'*40000
    LOCATION = 'wafwaf'*40000

    item.subject = SUBJECT
    item.html = HTML
    item[PidLidLocation] = LOCATION

    eid = item.entryid

    # stream via item.props()
    item = inbox.item(eid)  # avoid cache (no streaming?)

    for prop in item.props():
        if prop.proptag == PR_BODY_W:
            assert prop.value == BODY
        elif prop.proptag == PR_SUBJECT_W:
            assert prop.value == SUBJECT
        elif prop.namespace == 'appointment' and prop.name == 0x8208:
            assert prop.value == LOCATION

    # stream via item.prop()
    inbox = server.user(user.name).inbox
    item = inbox.item(eid)

    body = item.prop(PR_BODY_W).value
    assert body == BODY

    subject = item.prop(PR_SUBJECT_W).value
    assert subject == SUBJECT

    location = item.prop(PidLidLocation).value
    assert location == LOCATION


def test_kindname(item):
    item[PidLidLocation] = 'location'
    assert item.prop(PidLidLocation).kindname == 'MNID_ID'


def test_typename(item):
    item[PidLidLocation] = 'location'
    assert item.prop(PidLidLocation).typename == 'PT_UNICODE'


def test_strval(item):
    item.categories = ['cordon', 'blue']
    assert item.prop('public:Keywords').strval == 'cordon,blue'


def test_str(item):
    item.subject = ''
    assert str(item.prop(PR_SUBJECT_W)) == 'Property(PR_SUBJECT_W)'
