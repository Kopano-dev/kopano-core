import pytest


@pytest.mark.parametrize("flag", [True, False])
def test_seeprivate(delegate, flag):
    delegate.private = flag
    assert delegate.private == flag


@pytest.mark.parametrize("flag", [True, False])
def test_sendcopy(delegate, flag):
    delegate.send_copy = flag
    assert delegate.send_copy == flag


def test_flags(delegate):
    delegate.flags = ['see_private']
    assert delegate.see_private
    assert not delegate.send_copy

    delegate.flags = ['send_copy']
    assert not delegate.see_private
    assert delegate.send_copy

    delegate.flags = ['see_private', 'send_copy']
    assert delegate.flags == ['see_private', 'send_copy']

    delegate.see_private = False
    assert delegate.flags == ['send_copy']
    delegate.send_copy = False
    assert not delegate.flags


def test_str(delegate):
    assert str(delegate) == "Delegation('{}')".format(delegate.user.name)
