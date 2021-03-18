import pytest

from MAPI import (MAPI_UNICODE, MAPI_MODIFY, MAPI_E_NOT_FOUND, MAPI_E_COMPUTED,
                  KEEP_OPEN_READWRITE, MAPI_CREATE, ATTACH_EMBEDDED_MSG, MAPI_MOVE)
from MAPI.Time import FileTime, unixtime
from MAPI.Struct import SPropValue, PROP_TAG, PROP_ID, PROP_TYPE
from MAPI.Tags import (PT_NULL, PT_SHORT, PT_LONG, PT_FLOAT, PT_DOUBLE,
                       PT_CURRENCY, PT_APPTIME, PT_BOOLEAN, PT_LONGLONG,
                       PT_STRING8, PT_UNICODE, PT_SYSTIME, PT_CLSID, PT_BINARY,
                       PT_MV_LONG, PT_MV_SHORT, PT_MV_FLOAT, PT_MV_DOUBLE,
                       PT_MV_CURRENCY, PT_MV_APPTIME, PT_MV_CLSID,
                       PT_MV_LONGLONG, PT_MV_SYSTIME, PT_MV_UNICODE,
                       PT_MV_STRING8, PT_MV_BINARY,
                       PT_ERROR, PR_ENTRYID, PR_NORMALIZED_SUBJECT_A, PR_NORMALIZED_SUBJECT_W,
                       PR_SUBJECT_A, PR_SUBJECT_W, PR_SUBJECT_PREFIX_A, PR_SUBJECT_PREFIX_W,
                       PR_NORMALIZED_SUBJECT, PR_ATTACH_METHOD, PR_SOURCE_KEY,
                       PR_ATTACH_DATA_BIN, PR_RECORD_KEY, PR_EC_HIERARCHYID,
                       PR_ATTACH_DATA_OBJ, IID_IMessage)


def test_types(store, message):
    props = [
        SPropValue(PROP_TAG(PT_NULL, 0x6601), 0),
        SPropValue(PROP_TAG(PT_SHORT, 0x6602), 1),
        SPropValue(PROP_TAG(PT_LONG, 0x6603), 2),
        SPropValue(PROP_TAG(PT_FLOAT, 0x6604), 3),
        SPropValue(PROP_TAG(PT_DOUBLE, 0x6605), 4),
        SPropValue(PROP_TAG(PT_CURRENCY, 0x6606), 5),
        SPropValue(PROP_TAG(PT_APPTIME, 0x6607), 6),
        SPropValue(PROP_TAG(PT_BOOLEAN, 0x6608), True),
        SPropValue(PROP_TAG(PT_LONGLONG, 0x6609), 7),
        SPropValue(PROP_TAG(PT_STRING8, 0x6610), b'string8'),
        SPropValue(PROP_TAG(PT_UNICODE, 0x6611), u'こんにちは'),
        SPropValue(PROP_TAG(PT_SYSTIME, 0x6612), FileTime(120000000000000000)),
        SPropValue(PROP_TAG(PT_CLSID, 0x6613), b'1234567890123456'),
        SPropValue(PROP_TAG(PT_BINARY, 0x6614), b'binary')]

    message.SetProps(props)
    message.SaveChanges(0)

    entryidprops = message.GetProps([PR_ENTRYID], 0)
    entryid = entryidprops[0].Value
    message = store.OpenEntry(entryid, None, 0)

    results = message.GetProps([
                                PROP_TAG(PT_NULL, 0x6601),
                                PROP_TAG(PT_SHORT, 0x6602),
                                PROP_TAG(PT_LONG, 0x6603),
                                PROP_TAG(PT_FLOAT, 0x6604),
                                PROP_TAG(PT_DOUBLE, 0x6605),
                                PROP_TAG(PT_CURRENCY, 0x6606),
                                PROP_TAG(PT_APPTIME, 0x6607),
                                PROP_TAG(PT_BOOLEAN, 0x6608),
                                PROP_TAG(PT_LONGLONG, 0x6609),
                                PROP_TAG(PT_STRING8, 0x6610),
                                PROP_TAG(PT_UNICODE, 0x6611),
                                PROP_TAG(PT_SYSTIME, 0x6612),
                                PROP_TAG(PT_CLSID, 0x6613),
                                PROP_TAG(PT_BINARY, 0x6614)],
                               0)

    props.remove(SPropValue(PROP_TAG(PT_NULL, 0x6601), 0))
    props.insert(0, SPropValue(PROP_TAG(PT_ERROR, 0x6601), MAPI_E_NOT_FOUND))

    assert results == props


def test_types2(store, message):
    props = [
            SPropValue(PROP_TAG(PT_NULL, 0x6601), 0),
            SPropValue(PROP_TAG(PT_SHORT, 0x6602), -1),
            SPropValue(PROP_TAG(PT_LONG, 0x6603), -2),
            SPropValue(PROP_TAG(PT_FLOAT, 0x6604), -3),
            SPropValue(PROP_TAG(PT_DOUBLE, 0x6605), -4),
            SPropValue(PROP_TAG(PT_CURRENCY, 0x6606), -5),
            SPropValue(PROP_TAG(PT_APPTIME, 0x6607), -6),
            SPropValue(PROP_TAG(PT_BOOLEAN, 0x6608), False),
            SPropValue(PROP_TAG(PT_LONGLONG, 0x6609), -7),
            SPropValue(PROP_TAG(PT_STRING8, 0x6610), b''),
            SPropValue(PROP_TAG(PT_UNICODE, 0x6611), u'こんにちは'),
            SPropValue(PROP_TAG(PT_SYSTIME, 0x6612), unixtime(0)),
            SPropValue(PROP_TAG(PT_CLSID, 0x6613), b'1234567890123456'),
            SPropValue(PROP_TAG(PT_BINARY, 0x6614), b'')]

    message.SetProps(props)
    message.SaveChanges(0)

    entryidprops = message.GetProps([PR_ENTRYID], 0)
    entryid = entryidprops[0].Value
    message = store.OpenEntry(entryid, None, 0)

    results = message.GetProps([
                                PROP_TAG(PT_NULL, 0x6601),
                                PROP_TAG(PT_SHORT, 0x6602),
                                PROP_TAG(PT_LONG, 0x6603),
                                PROP_TAG(PT_FLOAT, 0x6604),
                                PROP_TAG(PT_DOUBLE, 0x6605),
                                PROP_TAG(PT_CURRENCY, 0x6606),
                                PROP_TAG(PT_APPTIME, 0x6607),
                                PROP_TAG(PT_BOOLEAN, 0x6608),
                                PROP_TAG(PT_LONGLONG, 0x6609),
                                PROP_TAG(PT_STRING8, 0x6610),
                                PROP_TAG(PT_UNICODE, 0x6611),
                                PROP_TAG(PT_SYSTIME, 0x6612),
                                PROP_TAG(PT_CLSID, 0x6613),
                                PROP_TAG(PT_BINARY, 0x6614)], 0)

    props.remove(SPropValue(PROP_TAG(PT_NULL, 0x6601), 0))
    props.insert(0, SPropValue(PROP_TAG(PT_ERROR, 0x6601), MAPI_E_NOT_FOUND))

    assert results == props


def test_mvprops(store, message):
    props = [
            SPropValue(PROP_TAG(PT_MV_SHORT, 0x6601), [1, 2, 3]),
            SPropValue(PROP_TAG(PT_MV_LONG, 0x6602), [1, 2, 3]),
            SPropValue(PROP_TAG(PT_MV_FLOAT, 0x6603), [1, 2, 3]),
            SPropValue(PROP_TAG(PT_MV_DOUBLE, 0x6604), [1, 2, 3]),
            SPropValue(PROP_TAG(PT_MV_CURRENCY, 0x6605), [1, 2, 3]),
            SPropValue(PROP_TAG(PT_MV_APPTIME, 0x6606), [1, 2, 3]),
            SPropValue(PROP_TAG(PT_MV_LONGLONG, 0x6607), [1, 2, 3]),
            SPropValue(PROP_TAG(PT_MV_STRING8, 0x6608), [b'a', b'b', b'c']),
            SPropValue(PROP_TAG(PT_MV_UNICODE, 0x6609), ['こん', 'に', 'ちは']),
            SPropValue(PROP_TAG(PT_MV_SYSTIME, 0x660a), [unixtime(0), unixtime(1), unixtime(2)]),
            SPropValue(PROP_TAG(PT_MV_CLSID, 0x660b), [b'1234567890123456', b'6543210987654321', b'0000000000000000']),
            SPropValue(PROP_TAG(PT_MV_BINARY, 0x660c), [b'bin', b'', b'data'])]

    message.SetProps(props)
    message.SaveChanges(0)

    entryidprops = message.GetProps([PR_ENTRYID], 0)
    entryid = entryidprops[0].Value
    message = store.OpenEntry(entryid, None, 0)

    results = message.GetProps([
                                PROP_TAG(PT_MV_SHORT, 0x6601),
                                PROP_TAG(PT_MV_LONG, 0x6602),
                                PROP_TAG(PT_MV_FLOAT, 0x6603),
                                PROP_TAG(PT_MV_DOUBLE, 0x6604),
                                PROP_TAG(PT_MV_CURRENCY, 0x6605),
                                PROP_TAG(PT_MV_APPTIME, 0x6606),
                                PROP_TAG(PT_MV_LONGLONG, 0x6607),
                                PROP_TAG(PT_MV_STRING8, 0x6608),
                                PROP_TAG(PT_MV_UNICODE, 0x6609),
                                PROP_TAG(PT_MV_SYSTIME, 0x660a),
                                PROP_TAG(PT_MV_CLSID, 0x660b),
                                PROP_TAG(PT_MV_BINARY, 0x660c)], 0)

    assert results == props


def test_mvtypes2(store, message):
    props = [
            SPropValue(PROP_TAG(PT_MV_SHORT, 0x6601), [-1, -2, -3]),
            SPropValue(PROP_TAG(PT_MV_LONG, 0x6602), [-1, -2, -3]),
            SPropValue(PROP_TAG(PT_MV_FLOAT, 0x6603), [-1, -2, -3]),
            SPropValue(PROP_TAG(PT_MV_DOUBLE, 0x6604), [-1, -2, -3]),
            SPropValue(PROP_TAG(PT_MV_CURRENCY, 0x6605), [-1, -2, -3]),
            SPropValue(PROP_TAG(PT_MV_APPTIME, 0x6606), [-1, -2, -3]),
            SPropValue(PROP_TAG(PT_MV_LONGLONG, 0x6607), [-1, -2, -3]),
            SPropValue(PROP_TAG(PT_MV_STRING8, 0x6608), [b'', b'', b'']),
            SPropValue(PROP_TAG(PT_MV_UNICODE, 0x6609), ['', '', '']),
            SPropValue(PROP_TAG(PT_MV_SYSTIME, 0x660a), [unixtime(0), unixtime(0), unixtime(0)]),
            SPropValue(PROP_TAG(PT_MV_CLSID, 0x660b), [b'0000000000000000', b'AAAAAAAAAAAAAAAA', b'FFFFFFFFFFFFFFFF']),
            SPropValue(PROP_TAG(PT_MV_BINARY, 0x660c), [b'', b'', b''])
    ]

    message.SetProps(props)
    message.SaveChanges(0)

    entryidprops = message.GetProps([PR_ENTRYID], 0)
    entryid = entryidprops[0].Value
    message = store.OpenEntry(entryid, None, 0)

    results = message.GetProps([
        PROP_TAG(PT_MV_SHORT, 0x6601),
        PROP_TAG(PT_MV_LONG, 0x6602),
        PROP_TAG(PT_MV_FLOAT, 0x6603),
        PROP_TAG(PT_MV_DOUBLE, 0x6604),
        PROP_TAG(PT_MV_CURRENCY, 0x6605),
        PROP_TAG(PT_MV_APPTIME, 0x6606),
        PROP_TAG(PT_MV_LONGLONG, 0x6607),
        PROP_TAG(PT_MV_STRING8, 0x6608),
        PROP_TAG(PT_MV_UNICODE, 0x6609),
        PROP_TAG(PT_MV_SYSTIME, 0x660a),
        PROP_TAG(PT_MV_CLSID, 0x660b),
        PROP_TAG(PT_MV_BINARY, 0x660c)], 0)

    assert results == props


def test_setpropsoutsidebmp(store, message):
    props = [
            SPropValue(PROP_TAG(PT_UNICODE, 0x6601), u'a\U00010000\U00100000\U00001000b'),
            SPropValue(PROP_TAG(PT_MV_UNICODE, 0x6602), [u'a\U00010000\U00100000\U00001000b'])
    ]

    message.SetProps(props)
    message.SaveChanges(0)
    entryid = message.GetProps([PR_ENTRYID], 0)[0].Value
    message = store.OpenEntry(entryid, None, MAPI_MODIFY)
    results = message.GetProps([x.ulPropTag for x in props], 0)

    assert results == props


def test_setpropshighchars(store, message):
    props = [
            SPropValue(PROP_TAG(PT_STRING8, 0x6600), 'ÌPM.Ñotè'.encode('utf8')),
            SPropValue(PROP_TAG(PT_UNICODE, 0x6601), 'ÌPM.Ñotè')]

    message.SetProps(props)
    message.SaveChanges(0)
    entryid = message.GetProps([PR_ENTRYID], 0)[0].Value
    message = store.OpenEntry(entryid, None, MAPI_MODIFY)
    results = message.GetProps([PROP_TAG(PT_STRING8, 0x6600), PROP_TAG(PT_UNICODE, 0x6601)], 0)

    assert results == props


def test_notfound(message):
    props = message.GetProps([PROP_TAG(PT_STRING8, 0x6600)], 0)
    assert props[0].ulPropTag == PROP_TAG(PT_ERROR, 0x6600)
    assert props[0].Value == MAPI_E_NOT_FOUND


def test_getproplist(message):
    message.SetProps([SPropValue(PROP_TAG(PT_STRING8, 0x6600), b'test')])
    props = message.GetPropList(0)
    found = False
    for p in props:
        if p == PROP_TAG(PT_STRING8, 0x6600):
            found = True
            break
    assert found


def test_deleteprop_doesnotexists(message):
    tag = PROP_TAG(PT_STRING8, 0x6600)
    problems = message.DeleteProps([tag])
    assert len(problems) == 1
    assert problems[0].ulIndex == 0
    assert PROP_ID(problems[0].ulPropTag) == PROP_ID(tag)
    assert problems[0].scode == MAPI_E_NOT_FOUND


def test_deleteprop_computed(message):
    problems = message.DeleteProps([PR_ENTRYID])
    assert len(problems) == 1
    assert problems[0].ulIndex == 0
    assert PROP_ID(problems[0].ulPropTag) == PROP_ID(PR_ENTRYID)
    assert problems[0].scode == MAPI_E_COMPUTED


def test_deleteprops_beforesave(message):
    tag = PROP_TAG(PT_STRING8, 0x6600)
    message.SetProps([SPropValue(tag, b'test')])
    message.DeleteProps([tag])
    props = message.GetProps([tag], 0)
    assert props[0].ulPropTag == PROP_TAG(PT_ERROR, 0x6600)


def test_deleteprops_difftypes(message):
    tag1 = PROP_TAG(PT_STRING8, 0x6600)
    tag2 = PROP_TAG(PT_LONG, 0x6600)
    message.SetProps([SPropValue(tag1, b'test')])
    problems = message.DeleteProps([tag2])
    assert not problems
    message.SaveChanges(KEEP_OPEN_READWRITE)
    props = message.GetProps([tag1], 0)
    assert props[0].ulPropTag == PROP_TAG(PT_ERROR, 0x6600)


def test_deleteprops_aftersave(message):
    tag = PROP_TAG(PT_STRING8, 0x6600)
    message.SetProps([SPropValue(tag, b'test')])
    message.DeleteProps([tag])
    message.SaveChanges(KEEP_OPEN_READWRITE)
    props = message.GetProps([tag], 0)
    assert props[0].ulPropTag == PROP_TAG(PT_ERROR, 0x6600)


def test_deleteprops_afterreopen(store, message):
    tag = PROP_TAG(PT_STRING8, 0x6600)
    message.SetProps([SPropValue(tag, b'test')])
    message.SaveChanges(0)
    props = message.GetProps([PR_ENTRYID], 0)
    entryid = props[0].Value
    message = store.OpenEntry(entryid, None, MAPI_MODIFY)
    message.DeleteProps([tag])
    message.SaveChanges(0)
    message = store.OpenEntry(entryid, None, 0)

    props = message.GetProps([tag], 0)
    assert props[0].ulPropTag == PROP_TAG(PT_ERROR, 0x6600)


def test_setprops_overwrite(store, message):
    tag = PROP_TAG(PT_STRING8, 0x6600)
    message.SetProps([SPropValue(tag, b'test')])
    message.SaveChanges(0)
    props = message.GetProps([PR_ENTRYID], 0)
    entryid = props[0].Value
    message = store.OpenEntry(entryid, None, MAPI_MODIFY)
    message.SetProps([SPropValue(tag, b'tset')])
    props = message.GetProps([tag], 0)
    assert props[0].ulPropTag == tag
    assert props[0].Value == b'tset'
    message.SaveChanges(0)
    message = store.OpenEntry(entryid, None, 0)
    props = message.GetProps([tag], 0)
    assert props[0].ulPropTag == tag
    assert props[0].Value == b'tset'


def test_setprops_partialcomputed(message):
    tag = PROP_TAG(PT_STRING8, 0x6600)
    problems = message.SetProps([SPropValue(tag, b'test'), SPropValue(PR_ENTRYID, b'')])
    props = message.GetProps([tag], 0)
    assert len(problems) == 1
    assert problems[0].ulIndex == 1
    assert problems[0].ulPropTag == PR_ENTRYID
    assert problems[0].scode == MAPI_E_COMPUTED
    assert props[0].ulPropTag == tag
    assert props[0].Value == b'test'

# A few important things that are expected from the subject, prefix and normalized subject:
# - The subject is equal to the prefix plus the normalized subject
# - The normalized subject is equal to the subject minus the prefix.
# - The prefix is calculated from the subject if:
#   - The subject contains a maximum 3 non numberic character string followed by a colon. Note: The MAPI documentation
#     states it needs a space afterwards but we also allow it without a space (I am not sure why we do this or if it's
#     a good idea). If a space is present after the colon, it will be part of the prefix as well.
# - The prefix can be set manually but, if it does not match the intial string of the subject, the normalized subject
#   will be equal to the subject. The MAPI documentation states that if this is the case we should try to compute it
#   from the subject. However we are not doing that. For a further explanation as to why please check the ECMessage.cpp
#   code.
# - If the prefix is set manually, we will no longer recalculate it when changing the subject, unless the message is
#   reloaded.
# The test_subject and test_subject_unicode tests attempt to tests these conditions.
def test_subject(store, message):
    # Normalized subject not found if no subject is present
    normalized = message.GetProps([PR_NORMALIZED_SUBJECT_A], 0)
    assert normalized == [SPropValue(PROP_TAG(PT_ERROR, PROP_ID(PR_NORMALIZED_SUBJECT_A)), MAPI_E_NOT_FOUND)]

    # Subject prefix is generated if it contains a maximum of 3 characters followed by a colon :
    message.SetProps([SPropValue(PR_SUBJECT_A, b'Re: test')])
    prefix = message.GetProps([PR_SUBJECT_PREFIX_A], 0)
    assert prefix == [SPropValue(PR_SUBJECT_PREFIX_A, b'Re: ')]
    normalized = message.GetProps([PR_NORMALIZED_SUBJECT_A], 0)
    assert normalized == [SPropValue(PR_NORMALIZED_SUBJECT_A, b'test')]

    # Although the MAPI documentation states a space must be present after the colon (:) we allow that in our code:
    # Set short prefix 'Re:', should clash with longer 'Re: ' prefix later
    message.SetProps([SPropValue(PR_SUBJECT_A, b'Re:factor')])
    subjects = message.GetProps([PR_SUBJECT_PREFIX_A, PR_NORMALIZED_SUBJECT_A], 0)
    assert subjects == [SPropValue(PR_SUBJECT_PREFIX_A, b'Re:'),SPropValue(PR_NORMALIZED_SUBJECT_A, b'factor')]

    # If prefix is larger than 3 characters it won't be calculated and the normalized property will be equal to the subject.
    # If the prefix is manually set and equal to the beginning of the subject it will be removed from the normalized.
    message.SetProps([SPropValue(PR_SUBJECT_A, b'Heelverhaal: test')])
    prefix = message.GetProps([PR_SUBJECT_PREFIX_A], 0)
    assert prefix == [SPropValue(PR_SUBJECT_PREFIX_A, b'')]
    normalized = message.GetProps([PR_NORMALIZED_SUBJECT_A], 0)
    assert normalized == [SPropValue(PR_NORMALIZED_SUBJECT_A, b'Heelverhaal: test')]
    message.SetProps([SPropValue(PR_SUBJECT_PREFIX_A, b'Heelverhaal: ')])
    normalized = message.GetProps([PR_NORMALIZED_SUBJECT_A], 0)
    assert normalized == [SPropValue(PR_NORMALIZED_SUBJECT_A, b'test')]

    # If the prefix is different than intial string of the subject, the normalized will be equal to the subject
    message.SetProps([SPropValue(PR_SUBJECT_A, b'Re: test'), SPropValue(PR_SUBJECT_PREFIX_A, b'Aap')])
    prefix = message.GetProps([PR_SUBJECT_PREFIX_A], 0)
    assert prefix == [SPropValue(PR_SUBJECT_PREFIX_A, b'Aap')]
    normalized = message.GetProps([PR_NORMALIZED_SUBJECT_A], 0)
    assert normalized == [SPropValue(PR_NORMALIZED_SUBJECT_A, b'Re: test')]

    # After forcing the prefix to be different, it won't be recalculated anymore
    message.SetProps([SPropValue(PR_SUBJECT_A, b'Re: test')])
    prefix = message.GetProps([PR_SUBJECT_PREFIX_A], 0)
    assert prefix == [SPropValue(PR_SUBJECT_PREFIX_A, b'Aap')]

    # After a reload we calculate the prefix again
    message.SaveChanges(KEEP_OPEN_READWRITE)
    entryid = message.GetProps([PR_ENTRYID], 0)
    m2 = store.OpenEntry(entryid[0].Value, None, MAPI_MODIFY)
    m2.SetProps([SPropValue(PR_SUBJECT_A, b'Re: test')])
    prefix = m2.GetProps([PR_SUBJECT_PREFIX_A], 0)
    assert prefix == [SPropValue(PR_SUBJECT_PREFIX_A, b'Re: ')]

    # And the original message after save changes will also recalculate the prefix
    message.SetProps([SPropValue(PR_SUBJECT_A, b'Re: test')])
    prefix = message.GetProps([PR_SUBJECT_PREFIX_A], 0)
    assert prefix == [SPropValue(PR_SUBJECT_PREFIX_A, b'Re: ')]

    # If we delete the message the prefix should not be found anymore
    message.DeleteProps([PR_SUBJECT_A])
    prefix = message.GetProps([PR_SUBJECT_PREFIX_A], 0)
    assert prefix == [SPropValue(PROP_TAG(PT_ERROR, PROP_ID(PR_SUBJECT_PREFIX_A)), MAPI_E_NOT_FOUND)]

    # If we set it explicitly and then delete the subject, we'll keep the prefix
    message.SetProps([SPropValue(PR_SUBJECT_A, b'Re: test'), SPropValue(PR_SUBJECT_PREFIX_A, b'Aap')])
    prefix = message.GetProps([PR_SUBJECT_PREFIX_A], 0)
    assert prefix == [SPropValue(PR_SUBJECT_PREFIX_A, b'Aap')]
    message.DeleteProps([PR_SUBJECT_A])
    subject = message.GetProps([PR_SUBJECT_PREFIX_A], 0)
    assert subject == [SPropValue(PR_SUBJECT_PREFIX_A, b'Aap')]

    # If we delete the prefix and then set the subject it will be synced again.
    message.DeleteProps([PR_SUBJECT_A, PR_SUBJECT_PREFIX_A])
    message.SetProps([SPropValue(PR_SUBJECT_A, b'Re:factor')])
    prefix = message.GetProps([PR_SUBJECT_PREFIX_A], 0)
    assert prefix == [SPropValue(PR_SUBJECT_PREFIX_A, b'Re:')]


def test_subject_unicode(store, message):
    # Normalized subject not found if no subject is present
    normalized = message.GetProps([PR_NORMALIZED_SUBJECT_W], 0)
    assert normalized == [SPropValue(PROP_TAG(PT_ERROR, PROP_ID(PR_NORMALIZED_SUBJECT_W)), MAPI_E_NOT_FOUND)]

    # Subject prefix is generated if it contains a maximum of 3 characters followed by a colon :
    message.SetProps([SPropValue(PR_SUBJECT_W, 'Re: test')])
    prefix = message.GetProps([PR_SUBJECT_PREFIX_W], 0)
    assert prefix == [SPropValue(PR_SUBJECT_PREFIX_W, 'Re: ')]
    normalized = message.GetProps([PR_NORMALIZED_SUBJECT_W], 0)
    assert normalized == [SPropValue(PR_NORMALIZED_SUBJECT_W, 'test')]

    # Prefix should be correctly calculated with characters that occupy more than 1 byte
    message.SetProps([SPropValue(PR_SUBJECT_W, '回复: japanese subject')])
    prefix = message.GetProps([PR_SUBJECT_PREFIX_W], 0)
    assert prefix == [SPropValue(PR_SUBJECT_PREFIX_W, '回复: ')]
    normalized = message.GetProps([PR_NORMALIZED_SUBJECT_W], 0)
    assert normalized == [SPropValue(PR_NORMALIZED_SUBJECT_W, 'japanese subject')]

    # Although the MAPI documentation states a space must be present after the colon (:) we allow that in our code:
    # Set short prefix 'Re:', should clash with longer 'Re: ' prefix later
    message.SetProps([SPropValue(PR_SUBJECT_W, 'Re:factor')])
    subjects = message.GetProps([PR_SUBJECT_PREFIX_W, PR_NORMALIZED_SUBJECT_W], 0)
    assert subjects == [SPropValue(PR_SUBJECT_PREFIX_W, 'Re:'),SPropValue(PR_NORMALIZED_SUBJECT_W, 'factor')]

    # If prefix is larger than 3 characters it won't be calculated and the normalized property will be equal to the subject.
    # If the prefix is manually set and equal to the beginning of the subject it will be removed from the normalized.
    message.SetProps([SPropValue(PR_SUBJECT_W, 'Heelverhaal: test')])
    prefix = message.GetProps([PR_SUBJECT_PREFIX_W], 0)
    assert prefix == [SPropValue(PR_SUBJECT_PREFIX_W, '')]
    normalized = message.GetProps([PR_NORMALIZED_SUBJECT_W], 0)
    assert normalized == [SPropValue(PR_NORMALIZED_SUBJECT_W, 'Heelverhaal: test')]
    message.SetProps([SPropValue(PR_SUBJECT_PREFIX_W, 'Heelverhaal: ')])
    normalized = message.GetProps([PR_NORMALIZED_SUBJECT_W], 0)
    assert normalized == [SPropValue(PR_NORMALIZED_SUBJECT_W, 'test')]

    # If the prefix is different than intial string of the subject, the normalized will be equal to the subject
    message.SetProps([SPropValue(PR_SUBJECT_W, 'Re: test'), SPropValue(PR_SUBJECT_PREFIX_W, 'Aap')])
    prefix = message.GetProps([PR_SUBJECT_PREFIX_W], 0)
    assert prefix == [SPropValue(PR_SUBJECT_PREFIX_W, 'Aap')]
    normalized = message.GetProps([PR_NORMALIZED_SUBJECT_W], 0)
    assert normalized == [SPropValue(PR_NORMALIZED_SUBJECT_W, 'Re: test')]

    # After forcing the prefix to be different, it won't be recalculated anymore
    message.SetProps([SPropValue(PR_SUBJECT_W, 'Re: test')])
    prefix = message.GetProps([PR_SUBJECT_PREFIX_W], 0)
    assert prefix == [SPropValue(PR_SUBJECT_PREFIX_W, 'Aap')]

    # After a reload we calculate the prefix again
    message.SaveChanges(KEEP_OPEN_READWRITE)
    entryid = message.GetProps([PR_ENTRYID], 0)
    m2 = store.OpenEntry(entryid[0].Value, None, MAPI_MODIFY)
    m2.SetProps([SPropValue(PR_SUBJECT_W, 'Re: test')])
    prefix = m2.GetProps([PR_SUBJECT_PREFIX_W], 0)
    assert prefix == [SPropValue(PR_SUBJECT_PREFIX_W, 'Re: ')]

    # And the original message after save changes will also recalculate the prefix
    message.SetProps([SPropValue(PR_SUBJECT_W, 'Re: test')])
    prefix = message.GetProps([PR_SUBJECT_PREFIX_W], 0)
    assert prefix == [SPropValue(PR_SUBJECT_PREFIX_W, 'Re: ')]

    # If we delete the message the prefix should not be found anymore
    message.DeleteProps([PR_SUBJECT_W])
    prefix = message.GetProps([PR_SUBJECT_PREFIX_W], 0)
    assert prefix == [SPropValue(PROP_TAG(PT_ERROR, PROP_ID(PR_SUBJECT_PREFIX_W)), MAPI_E_NOT_FOUND)]

    # If we set it explicitly and then delete the subject, we'll keep the prefix
    message.SetProps([SPropValue(PR_SUBJECT_W, 'Re: test'), SPropValue(PR_SUBJECT_PREFIX_W, 'Aap')])
    prefix = message.GetProps([PR_SUBJECT_PREFIX_W], 0)
    assert prefix == [SPropValue(PR_SUBJECT_PREFIX_W, 'Aap')]
    message.DeleteProps([PR_SUBJECT_W])
    subject = message.GetProps([PR_SUBJECT_PREFIX_W], 0)
    assert subject == [SPropValue(PR_SUBJECT_PREFIX_W, 'Aap')]

    # If we delete the prefix and then set the subject it will be synced again.
    message.DeleteProps([PR_SUBJECT_W, PR_SUBJECT_PREFIX_W])
    message.SetProps([SPropValue(PR_SUBJECT_W, 'Re:factor')])
    prefix = message.GetProps([PR_SUBJECT_PREFIX_W], 0)
    assert prefix == [SPropValue(PR_SUBJECT_PREFIX_W, 'Re:')]

def test_subject_nonalphaprefix(message):
    message.SetProps([SPropValue(PR_SUBJECT_A, b'D&D: Hallo')])
    props = message.GetProps([PR_SUBJECT_A, PR_SUBJECT_PREFIX_A, PR_NORMALIZED_SUBJECT], 0)
    assert props == [SPropValue(PR_SUBJECT_A, b'D&D: Hallo'), SPropValue(PR_SUBJECT_PREFIX_A, b'D&D: '), SPropValue(PR_NORMALIZED_SUBJECT, b'Hallo')]


def test_subject_numericprefix(message):
    message.SetProps([SPropValue(PR_SUBJECT_A, b'15:00 taart!')])
    props = message.GetProps([PR_SUBJECT_A, PR_SUBJECT_PREFIX_A, PR_NORMALIZED_SUBJECT], 0)
    assert props == [SPropValue(PR_SUBJECT_A, b'15:00 taart!'), SPropValue(PR_SUBJECT_PREFIX_A, b''), SPropValue(PR_NORMALIZED_SUBJECT, b'15:00 taart!')]


def test_copyprops(root, message):
    tags = [
            PROP_TAG(PT_SHORT, 0x6602),
            PROP_TAG(PT_LONG, 0x6603),
            PROP_TAG(PT_FLOAT, 0x6604),
            PROP_TAG(PT_DOUBLE, 0x6605),
            PROP_TAG(PT_CURRENCY, 0x6606),
            PROP_TAG(PT_APPTIME, 0x6607),
            PROP_TAG(PT_BOOLEAN, 0x6608),
            PROP_TAG(PT_LONGLONG, 0x6609),
            PROP_TAG(PT_STRING8, 0x6610),
            PROP_TAG(PT_UNICODE, 0x6611),
            PROP_TAG(PT_SYSTIME, 0x6612),
            PROP_TAG(PT_CLSID, 0x6613),
            PROP_TAG(PT_BINARY, 0x6614)]

    props = [
            SPropValue(PROP_TAG(PT_SHORT, 0x6602), 1),
            SPropValue(PROP_TAG(PT_LONG, 0x6603), 2),
            SPropValue(PROP_TAG(PT_FLOAT, 0x6604), 3),
            SPropValue(PROP_TAG(PT_DOUBLE, 0x6605), 4),
            SPropValue(PROP_TAG(PT_CURRENCY, 0x6606), 5),
            SPropValue(PROP_TAG(PT_APPTIME, 0x6607), 6),
            SPropValue(PROP_TAG(PT_BOOLEAN, 0x6608), True),
            SPropValue(PROP_TAG(PT_LONGLONG, 0x6609), 7),
            SPropValue(PROP_TAG(PT_STRING8, 0x6610), b'string8'),
            SPropValue(PROP_TAG(PT_UNICODE, 0x6611), u'こんにちは'),
            SPropValue(PROP_TAG(PT_SYSTIME, 0x6612), FileTime(120000000000000000)),
            SPropValue(PROP_TAG(PT_CLSID, 0x6613), b'1234567890123456'),
            SPropValue(PROP_TAG(PT_BINARY, 0x6614), b'binary')]

    message.SetProps(props)

    msgnew = root.CreateMessage(None, 0)
    message.CopyProps(tags, 0, None, IID_IMessage, msgnew, 0)
    results = msgnew.GetProps(tags, 0)
    assert results == props

    # move the props
    msgnew = root.CreateMessage(None, 0)
    message.CopyProps(tags, 0, None, IID_IMessage, msgnew, MAPI_MOVE)
    results = msgnew.GetProps(tags, 0)
    assert results == props

    results = message.GetProps(tags, 0)

    for i in results:
        assert PROP_TYPE(i.ulPropTag) == PT_ERROR


def test_hiearchyid(store, message):
    # get on an unsaved prop, returns not found
    props = message.GetProps([PR_EC_HIERARCHYID], 0)
    assert props[0].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_EC_HIERARCHYID))
    assert props[0].Value == MAPI_E_NOT_FOUND
    # must be present after save/reopen
    message.SaveChanges(0)
    hid = message.GetProps([PR_EC_HIERARCHYID], 0)[0].Value
    eid = message.GetProps([PR_ENTRYID], 0)[0].Value

    message = store.OpenEntry(eid, None, 0)
    # first prop to get is generated, loadObject did not happen, but should
    assert message.GetProps([PR_EC_HIERARCHYID], 0)[0].Value == hid


def test_recordkey(message):
    props = message.GetProps([PR_RECORD_KEY], 0)
    assert props[0] == SPropValue(PROP_TAG(PT_ERROR, PROP_ID(PR_RECORD_KEY)), MAPI_E_NOT_FOUND)
    message.SaveChanges(KEEP_OPEN_READWRITE)
    props = message.GetProps([PR_RECORD_KEY, PR_ENTRYID], 0)
    assert props[0].ulPropTag == PR_RECORD_KEY
    assert len(props[0].Value) == 48
    assert props[0].Value == props[1].Value

    (id, att) = message.CreateAttach(None, 0)
    att.SetProps([SPropValue(PR_ATTACH_METHOD, ATTACH_EMBEDDED_MSG)])
    sub = att.OpenProperty(PR_ATTACH_DATA_OBJ, IID_IMessage, 0, MAPI_CREATE | MAPI_MODIFY)
    props = sub.GetProps([PR_RECORD_KEY, PR_ENTRYID, PR_SOURCE_KEY], 0)

    # Unsaved submessages have a source key but no record key or entryid
    assert props[0].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_RECORD_KEY))
    assert props[0].Value == MAPI_E_NOT_FOUND
    assert props[1].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_ENTRYID))
    assert props[1].Value == MAPI_E_NOT_FOUND
    assert props[2].ulPropTag == PR_SOURCE_KEY
    assert len(props[2].Value) == 22

    sub.SaveChanges(0)
    att.SaveChanges(0)
    message.SaveChanges(0)

    att = message.OpenAttach(0, None, 0)
    sub = att.OpenProperty(PR_ATTACH_DATA_OBJ, IID_IMessage, 0, 0)
    props = sub.GetProps([PR_RECORD_KEY, PR_ENTRYID, PR_SOURCE_KEY], 0)

    # Saved submessages have a PR_SOURCE_KEY but no record key or entryid
    assert props[0].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_RECORD_KEY))
    assert props[0].Value == MAPI_E_NOT_FOUND
    assert props[1].ulPropTag == PROP_TAG(PT_ERROR, PROP_ID(PR_ENTRYID))
    assert props[1].Value == MAPI_E_NOT_FOUND
    assert props[2].ulPropTag == PR_SOURCE_KEY
    assert len(props[2].Value) == 22
