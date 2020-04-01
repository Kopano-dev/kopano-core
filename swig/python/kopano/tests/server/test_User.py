import pytest

from MAPI.Tags import PR_SMTP_ADDRESS_W


def test_store(user):
    assert user.store.user.userid == user.userid


def test_active(user):
    assert user.active


def test_admin(user, useradmin):
    assert not user.admin
    assert useradmin.admin


def test_admin_level(user, useradmin):
    assert useradmin.admin_level == 1
    assert user.admin_level == 0


def test_email(user):
    assert user.email


def test_company(user):
    assert user.company.name == 'Default'


def test_attributes(user):
    assert user.home_server
    assert user.local
    assert not user.archive_server
    assert not user.archive_store


def test_cmp(user, user2):
    assert user == user
    assert user != user2


def test_prop(user):
    assert user.prop(PR_SMTP_ADDRESS_W).value == user.email


def test_props(user):
    props = list(user.props())
    prop = [prop for prop in props if prop.proptag == PR_SMTP_ADDRESS_W]
    assert len(prop)


def test_features(user):
    # LDAP has everything enabled
    assert user.features


def test_hidden(user):
    assert not user.hidden


def test_rules(user):
    assert not list(user.rules())


def test_sendas(user):
    assert not list(user.send_as())


def test_groups(user):
    # Always part of Everyone
    assert list(user.groups())


def test_str(user):
    assert str(user) == "User({})".format(user.name)
