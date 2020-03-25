from kopano import Address

USERID = b'\x00\x00\x00\x00\xac!\xa9P@\xd4\xeeH\xb2\x11\xfb\xa7S0D%\x01\x00\x00\x00\x06\x00\x00\x00\x03\x00\x00\x00MQ==\x00\x00\x00\x00'


def test_email(user, address):
    assert user.email == address.email


def test_name(user, address):
    assert user.name == address.name


def test_emptyname(empty_address):
    assert empty_address.name == ''


def test_empty_email_external(empty_address):
    assert empty_address.email == ''


def test_empty_email_kopano1(server, user):
    address = Address(server, "ZARAFA", user.name, None, USERID)
    assert address.email == ''


def test_empty_email_kopano2(server, user):
    searchkey = b'ZARAFA:%s\x00' % user.email.upper().encode()
    address = Address(server, "ZARAFA", user.name, None,
                      USERID, searchkey)
    assert address.email == user.email


def test_str(user, address):
    assert str(address) == 'Address({})'.format(user.name)

